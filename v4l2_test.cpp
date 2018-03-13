/*
Author:Jack-Cui
Blog:http://blog.csdn.net/c406495762
Time:25 May 2017
*/
#include <unistd.h>
#include <error.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <pthread.h>
#include <linux/videodev2.h>
#include <opencv/cv.h>
#include <sys/mman.h>
#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <iomanip>
#include <string>

using namespace std;

#define CLEAR(x) memset(&(x), 0, sizeof(x))

typedef enum
{
	I420, //yyyy.....uu....vv...
	YV12, //yyyy.....vv....uu...
	NV21, //yyyy.....vuvu....
	NV12  //yyyy.....uvuv....
} pix_fmt;

int g_width  = 1920;
int g_height = 1080;
int g_vnode  = 0;
int g_pixfmt = 0;
int g_recfmt = NV21;
int g_recframe = 0;
char g_recfile[32];

class V4L2Capture {
public:
	V4L2Capture(char *devName, int width, int height, int fmt = V4L2_PIX_FMT_MJPEG);
	virtual ~V4L2Capture();

	int openDevice();
	int closeDevice();
	int initDevice();
	int startCapture();
	int stopCapture();
	int freeBuffers();
	int getFrame(void **,size_t *);
	int backFrame();

private:
	int initBuffers();

	struct cam_buffer
	{
		void* start;
		unsigned int length;
	};
	char *devName;
	int capW;
	int capH;
	int pix_fmt;
	int fd_cam;
	cam_buffer *buffers;
	unsigned int n_buffers;
	int frameIndex;
};

V4L2Capture::V4L2Capture(char *devName, int width, int height, int fmt) {
	// TODO Auto-generated constructor stub
	this->devName = devName;
	this->fd_cam = -1;
	this->buffers = NULL;
	this->n_buffers = 0;
	this->frameIndex = -1;
	this->capW=width;
	this->capH=height;
	this->pix_fmt=fmt;
}

V4L2Capture::~V4L2Capture() {
	// TODO Auto-generated destructor stub
}

int V4L2Capture::openDevice() {
	printf("video dev : %s\n", devName);
	fd_cam = open(devName, O_RDWR);
	if (fd_cam < 0) {
		perror("Can't open video device");
	}
	return 0;
}

int V4L2Capture::closeDevice() {
	if (fd_cam > 0) {
		int ret = 0;
		if ((ret = close(fd_cam)) < 0) {
			perror("Can't close video device");
		}
		return 0;
	} else {
		return -1;
	}
}

int V4L2Capture::initDevice() {
	int ret;
	struct v4l2_capability cam_cap;
	struct v4l2_cropcap cam_cropcap;
	struct v4l2_fmtdesc cam_fmtdesc;
	struct v4l2_crop cam_crop;
	struct v4l2_format cam_format;

	ret = ioctl(fd_cam, VIDIOC_QUERYCAP, &cam_cap);
	if (ret < 0) {
		perror("Can't get device information: VIDIOCGCAP");
	}
	printf(
			"Driver Name:%s\nCard Name:%s\nBus info:%s\nDriver Version:%u.%u.%u\n",
			cam_cap.driver, cam_cap.card, cam_cap.bus_info,
			(cam_cap.version >> 16) & 0XFF, (cam_cap.version >> 8) & 0XFF,
			cam_cap.version & 0XFF);

	cam_fmtdesc.index = 0;
	cam_fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	printf("Support format:\n");
	while (ioctl(fd_cam, VIDIOC_ENUM_FMT, &cam_fmtdesc) != -1) {
		printf("\t%d.%s\n", cam_fmtdesc.index + 1, cam_fmtdesc.description);
		cam_fmtdesc.index++;
	}

	cam_cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (0 == ioctl(fd_cam, VIDIOC_CROPCAP, &cam_cropcap)) {
		printf("Default rec:\n\tleft:%d\n\ttop:%d\n\twidth:%d\n\theight:%d\n",
				cam_cropcap.defrect.left, cam_cropcap.defrect.top,
				cam_cropcap.defrect.width, cam_cropcap.defrect.height);

		cam_crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cam_crop.c = cam_cropcap.defrect;
		if (-1 == ioctl(fd_cam, VIDIOC_S_CROP, &cam_crop)) {
			//printf("Can't set crop para\n");
		}
	} else {
		printf("Can't set cropcap para\n");
	}

	cam_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	cam_format.fmt.pix.width = capW;
	cam_format.fmt.pix.height = capH;
	cam_format.fmt.pix.pixelformat = pix_fmt;//V4L2_PIX_FMT_YUYV V4L2_PIX_FMT_MJPEG
	cam_format.fmt.pix.field = V4L2_FIELD_INTERLACED;
	ret = ioctl(fd_cam, VIDIOC_S_FMT, &cam_format);
	if (ret < 0) {
		perror("Can't set frame information");
	}

	cam_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd_cam, VIDIOC_G_FMT, &cam_format);
	if (ret < 0) {
		perror("Can't get frame information");
	}
	printf("Current data format information:\n\twidth:%d\n\theight:%d\n\tpix:%d(%d)\n",
			cam_format.fmt.pix.width, cam_format.fmt.pix.height, cam_format.fmt.pix.pixelformat, V4L2_PIX_FMT_MJPEG);
	ret = initBuffers();
	if (ret < 0) {
		perror("Buffers init error");
		//exit(-1);
	}
	return 0;
}

int V4L2Capture::initBuffers() {
	int ret;
	struct v4l2_requestbuffers req;
	CLEAR(req);
	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(fd_cam, VIDIOC_REQBUFS, &req);
	if (ret < 0) {
		perror("Request frame buffers failed");
	}
	if (req.count < 2) {
		perror("Request frame buffers while insufficient buffer memory");
	}
	buffers = (struct cam_buffer*) calloc(req.count, sizeof(*buffers));
	if (!buffers) {
		perror("Out of memory");
	}
	for (n_buffers = 0; n_buffers < req.count; n_buffers++) {
		struct v4l2_buffer buf;
		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_buffers;
		ret = ioctl(fd_cam, VIDIOC_QUERYBUF, &buf);
		if (ret < 0) {
			printf("VIDIOC_QUERYBUF %d failed\n", n_buffers);
			return -1;
		}
		buffers[n_buffers].length = buf.length;
		//printf("buf.length= %d\n",buf.length);
		buffers[n_buffers].start = mmap(
				NULL, // start anywhere
				buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_cam,
				buf.m.offset);
		if (MAP_FAILED == buffers[n_buffers].start) {
			printf("mmap buffer%d failed\n", n_buffers);
			return -1;
		}
	}
	return 0;
}

int V4L2Capture::startCapture() {
	unsigned int i;
	for (i = 0; i < n_buffers; i++) {
		struct v4l2_buffer buf;
		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (-1 == ioctl(fd_cam, VIDIOC_QBUF, &buf)) {
			printf("VIDIOC_QBUF buffer%d failed\n", i);
			return -1;
		}
	}
	enum v4l2_buf_type type;
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == ioctl(fd_cam, VIDIOC_STREAMON, &type)) {
		printf("VIDIOC_STREAMON error");
		return -1;
	}
	return 0;
}

int V4L2Capture::stopCapture() {
	enum v4l2_buf_type type;
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == ioctl(fd_cam, VIDIOC_STREAMOFF, &type)) {
		printf("VIDIOC_STREAMOFF error\n");
		return -1;
	}
	return 0;
}

int V4L2Capture::freeBuffers() {
	unsigned int i;
	for (i = 0; i < n_buffers; ++i) {
		if (-1 == munmap(buffers[i].start, buffers[i].length)) {
			printf("munmap buffer%d failed\n", i);
			return -1;
		}
	}
	free(buffers);
	return 0;
}

int V4L2Capture::getFrame(void **frame_buf, size_t* len) {
	struct v4l2_buffer queue_buf;
	CLEAR(queue_buf);
	queue_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	queue_buf.memory = V4L2_MEMORY_MMAP;
	if (-1 == ioctl(fd_cam, VIDIOC_DQBUF, &queue_buf)) {
		printf("VIDIOC_DQBUF error\n");
		return -1;
	}
	*frame_buf = buffers[queue_buf.index].start;
	*len = buffers[queue_buf.index].length;
	frameIndex = queue_buf.index;
	return 0;
}

int V4L2Capture::backFrame() {
	if (frameIndex != -1) {
		struct v4l2_buffer queue_buf;
		CLEAR(queue_buf);
		queue_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		queue_buf.memory = V4L2_MEMORY_MMAP;
		queue_buf.index = frameIndex;
		if (-1 == ioctl(fd_cam, VIDIOC_QBUF, &queue_buf)) {
			printf("VIDIOC_QBUF error\n");
			return -1;
		}
		return 0;
	}
	return -1;
}

//yuvI420 to yuv420sp, yyyy....uu....vv.... to NV21 (yyyy.....vuvu) or NV12 (yyyy.....uvuv)
int yuvI420_2_nv(char* nv21, char* yuv420, int width, int height, int mode)
{
    char *y, *vu;
    int index_y, index_u, index_v;
    int i, j;

    if(mode != NV12 && mode != NV21){
        printf("Failed!! mode select NV12(%d) or NV21(%d), current mode is %d\n", NV12, NV21, mode);
        return -1;
    }
    y  = nv21;
    vu = nv21 + (width * height);

    for(j=0; j< height; j++)
    {
        for(i=0; i<width; i++)
        {
            *(y++) = yuv420[i + width*j];
        }
    }

    for(j=0; j< (width * height)/4; j++)
    {
    	if(mode == NV12){
    	    *(vu++) = yuv420[width * height + j];
    	    *(vu++) = yuv420[(int)((width * height)*5/4) + j];
    	}else if(mode == NV21){
    	    *(vu++) = yuv420[(int)((width * height)*5/4) + j];
    	    *(vu++) = yuv420[width * height + j];
    	}
    }
    return 0;
}

//yuvI420 to yuv420sp, yyyy....vv....uu.... to NV21 (yyyy.....vuvu) or NV12 (yyyy.....uvuv)
int yv12_2_nv(char* nv21, char* yuv420, int width, int height, int mode)
{
    char *y, *vu;
    int index_y, index_u, index_v;
    int i, j;

    if(mode != NV12 && mode != NV21){
        printf("Failed!! mode select NV12(%d) or NV21(%d), current mode is %d\n", NV12, NV21, mode);
        return -1;
    }
    y  = nv21;
    vu = nv21 + (width * height);

    for(j=0; j< height; j++)
    {
        for(i=0; i<width; i++)
        {
            *(y++) = yuv420[i + width*j];
        }
    }

    for(j=0; j< (width * height)/4; j++)
    {
    	if(mode == NV21){
    	    *(vu++) = yuv420[width * height + j];
    	    *(vu++) = yuv420[(int)((width * height)*5/4) + j];
    	}else if(mode == NV12){
    	    *(vu++) = yuv420[(int)((width * height)*5/4) + j];
    	    *(vu++) = yuv420[width * height + j];
    	}
    }
    return 0;
}

//yuv422 to yuv420p, yuyv.... to I420 (yyyy....uu....vv...) or YV12 (yyyy....vv....uu...)
int yuv422_2_yuv420(char* yuv420, char* yuv422, int width, int height, int mode)
{
	int imgSize = width * height * 2;
	int widthStep422 = width * 2;
	char* p422 = yuv422;
	char* p420y = yuv420;
	char* p420u;
	char* p420v;

	if(mode == I420){
		//CV_YUV2BGR_I420  yyyy.....uu....vv...
	    p420u = yuv420 + imgSize / 2;
	    p420v = p420u + imgSize / 8;
	}else if(mode == YV12){
		//CV_YUV2BGR_YV12  yyyy.....vv....uu...
		p420v = yuv420 + imgSize / 2;
		p420u = p420v + imgSize / 8;
	}else{
		printf("Failed!! mode select I420(%d) or YV12(%d), current mode is %d\n", I420, YV12, mode);
		return -1;
	}

	for(int i = 0; i < height; i += 2)
	{
		p422 = yuv422 + i * widthStep422;
		for(int j = 0; j < widthStep422; j+=4)
		{
			*(p420y++) = p422[j];
			*(p420u++) = p422[j+1];
			*(p420y++) = p422[j+2];
		}
		p422 += widthStep422;
		for(int j = 0; j < widthStep422; j+=4)
		{
			*(p420y++) = p422[j];
			*(p420v++) = p422[j+3];
			*(p420y++) = p422[j+2];
		}
	}
	return 0;
}

void VideoPlayer_YUV422(int w, int h, string videoDev) {
	unsigned long yuvframeSize = 0;
    cv::Mat yuvImg, rgbImg, outNV21, yuv420;
    yuvImg.create(h, w, CV_8UC2);
    rgbImg.create(h, w, CV_8UC3);
    yuv420.create(h*1.5, w, CV_8UC1);
    outNV21.create(h*1.5, w, CV_8UC1);
    double tt;
    int frame_cnt = 0;
    int yuv_length = g_height * g_width * 1.5;
    FILE * record_file = NULL;

    if(strlen(g_recfile) != 0){
    	printf("Start rec mode (%s)\n", g_recfile);
		record_file = fopen(g_recfile, "wa+");
		if (record_file == NULL) {
			printf("error : Open the file fail! \n");
		}
    }

	V4L2Capture *vcap = new V4L2Capture(const_cast<char*>(videoDev.c_str()), w, h, V4L2_PIX_FMT_YUYV);
	vcap->openDevice();
	vcap->initDevice();
	vcap->startCapture();

    while(1){
    	tt = (double)cvGetTickCount();
		vcap->getFrame((void **) &yuvImg.data, (size_t *)&yuvframeSize);

		if(record_file != NULL){
			yuv422_2_yuv420((char *)yuv420.data, (char *)yuvImg.data, g_width, g_height, I420);
			yuvI420_2_nv((char *)outNV21.data, (char *)yuv420.data, g_width, g_height, g_recfmt);
			fwrite((char *)outNV21.data, yuv_length, 1, record_file);
			//for check use https://docs.opencv.org/3.1.0/df/d4e/group__imgproc__c.html
			cv::cvtColor(outNV21, rgbImg, CV_YUV2BGR_NV21);
		}else{
		    cv::cvtColor(yuvImg, rgbImg, CV_YUV2BGR_YUYV);
		}

		cv::imshow("VideoPlayer", rgbImg);
		vcap->backFrame();
		if((cv::waitKey(1)&255) == 27)
			exit(0);

		if(frame_cnt++ > g_recframe && g_recframe)
			break;
		printf("Capture one frame time is %g ms\n",( ((double)cvGetTickCount() - tt) / (cvGetTickFrequency()*1000)));
	}

	if (record_file != NULL)
		fclose(record_file);

    vcap->stopCapture();
	vcap->freeBuffers();
	vcap->closeDevice();
}

void VideoPlayer_MJPEG_CV2(int w, int h, string videoDev) {
	unsigned char *yuv422frame = NULL;
	unsigned long yuvframeSize = 0;
	IplImage* img;
	CvMat cvmat;
	double tt;

	V4L2Capture *vcap = new V4L2Capture(const_cast<char*>(videoDev.c_str()), w, h, V4L2_PIX_FMT_MJPEG);
	vcap->openDevice();
	vcap->initDevice();
	vcap->startCapture();

	cvNamedWindow("MJPEG CV2",CV_WINDOW_AUTOSIZE);
	while(1){
		tt = (double)cvGetTickCount();
		vcap->getFrame((void **) &yuv422frame, (size_t *)&yuvframeSize);
		cvmat = cvMat(h,w,CV_8UC3,(void*)yuv422frame);		//CV_8UC3

		img = cvDecodeImage(&cvmat,1);
		if(!img){
			printf("DecodeImage error!\n");
		}

		cvShowImage("MJPEG CV2",img);

		vcap->backFrame();
		if((cvWaitKey(1)&255) == 27){
			exit(0);
		}
		printf("4Used time is %g ms\n",( ((double)cvGetTickCount() - tt) / (cvGetTickFrequency()*1000)));
	}

	vcap->stopCapture();
	vcap->freeBuffers();
	vcap->closeDevice();

}

void VideoPlayer_MJPEG_CV(int w, int h, string videoDev) {
	unsigned long yuvframeSize = 0;
    cv::Mat rgbImg;
    rgbImg.create(h, w, CV_8UC3);
	double tt;

	V4L2Capture *vcap = new V4L2Capture(const_cast<char*>(videoDev.c_str()), w, h, V4L2_PIX_FMT_MJPEG);
	vcap->openDevice();
	vcap->initDevice();
	vcap->startCapture();

	while(1){
		tt = (double)cvGetTickCount();
		vcap->getFrame((void **) &rgbImg.data, (size_t *)&yuvframeSize);
		rgbImg = cv::imdecode(cv::Mat(rgbImg), 1);

		cv::imshow("MJPEG CV", rgbImg);

		vcap->backFrame();
		if((cv::waitKey(1)) == 27){
			exit(0);
		}
		printf("3Used time is %g ms\n",( ((double)cvGetTickCount() - tt) / (cvGetTickFrequency()*1000)));
	}

	vcap->stopCapture();
	vcap->freeBuffers();
	vcap->closeDevice();

}

void parse_opt(int argc, char** argv)
{
    if(argc >= 3){
	    for(int i = 1; i < argc; i+=1){
            if(!strcmp(argv[i], "-camera")){
            	g_vnode = atoi(argv[++i]);
            }if(!strcmp(argv[i], "-fmt")){
            	g_pixfmt = atoi(argv[++i]);
            }else if(!strcmp(argv[i], "-res")){
				vector<char*> str;
				str.push_back(strtok (argv[++i],"*"));
				str.push_back(strtok (NULL, "*"));
				g_width = atoi(str[0]);
				g_height = atoi(str[1]);
			}else if(!strcmp(argv[i], "-recfile")){
				strcpy(g_recfile, argv[++i]);
			}else if(!strcmp(argv[i], "-recframe")){
				g_recframe = atoi(argv[++i]);
			}else if(!strcmp(argv[i], "-recfmt")){
				g_recfmt = atoi(argv[++i]);
			}
		}
    }
    printf("===============Video Info S ===============\n");
    printf("Open device: /dev/video%d\nCamera outout format: %s\nResulation: %d x %d\n"
    		, g_vnode, g_pixfmt? "MJPEG":"YUYV", g_width, g_height);
    if(strlen(g_recfile) != 0){
        printf("Recorder file: %s\nRecorder frame: %d\nRecorder format: %d\n", g_recfile, g_recframe, g_recfmt);
        printf("\t%d)I420 %d)YV12 %d)NV21 %d)NV12\n", I420, YV12, NV21, NV12);
    }
    printf("===============Video Info E ===============\n");
}

// ./v4l2_mjpeg_test -camera 0 -res 1920*1080 -fmt 1
int main(int argc, char** argv) {

	parse_opt(argc, argv);
	stringstream ss;
	ss << g_vnode;
	string node = "/dev/video" + ss.str();

	if(g_pixfmt == 0){
		//V4L2_PIX_FMT_YUYV
	    VideoPlayer_YUV422(g_width, g_height, node);
	}else{
		// V4L2_PIX_FMT_MJPEG
	    VideoPlayer_MJPEG_CV(g_width, g_height, node);
	    //VideoPlayer_MJPEG_CV2(g_width, g_height, node);
	}
	return 0;
}
