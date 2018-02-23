##############################################
# Tool Chain
##############################################
MY_CC := $(CXX)

TARGET              := v4l2_test

##############################################
# PATH
##############################################

##############################################
# Environment
##############################################
INC                 := ./ 

LIB                 := 

OBJ                 := 

DEFINES             := 

CFLAGS              := -lavformat -lavcodec -lavutil -lswscale -lm -lz -lpthread -fPIC -lopencv_highgui -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lopencv_videoio -lopencv_calib3d

##############################################
# Prepare
##############################################

##############################################
# Make
##############################################
all: $(OBJ) $(TARGET)


$(TARGET): $(TARGET).cpp $(OBJ)
	$(MY_CC) -o $@ $^ $(CFLAGS) $(patsubst %,-I%,$(INC))

%.o: %.cpp
	$(MY_CC) -c $< -o $@ $(CFLAGS)

clean:
	@$(RM) -rf *.o
	@$(RM) $(TARGET)
