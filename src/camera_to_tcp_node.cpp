#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <string>

#include <ros/ros.h>
#include <image_transport/image_transport.h>

#include <opencv2/opencv.hpp>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/highgui/highgui.hpp>

#include <sensor_msgs/image_encodings.h>
namespace enc = sensor_msgs::image_encodings;

#define SLEEP_US 33333 // 30FPS
#define MAGIC 0x17923349ab10ea9aL

int sock;
cv_bridge::CvImage bridge_;

static int64_t utime_now()
{
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (int64_t) tv.tv_sec * 1000000 + tv.tv_usec;
}

static void write_i32(uint8_t *buf, int32_t v)
{
    buf[0] = (v >> 24) & 0xFF;
    buf[1] = (v >> 16) & 0xFF;
    buf[2] = (v >>  8) & 0xFF;
    buf[3] = (v      ) & 0xFF;
}

static void write_i64(uint8_t *buf, int64_t v)
{
    uint32_t h = (uint32_t) (v >> 32);
    uint32_t l = (uint32_t) (v);

    write_i32(buf+0, h);
    write_i32(buf+4, l);
}

void imageCallback(const sensor_msgs::ImageConstPtr& msg)
{
  cv_bridge::CvImageConstPtr cv_ptr;
	try
	{
    cv_ptr = cv_bridge::toCvShare(msg, enc::MONO8);
	}
	catch(cv_bridge::Exception& e)
	{
		ROS_ERROR("cv_bridge exception: %s", e.what());
		return;
	}

  int len = -1;

  int64_t magic = MAGIC;

  int64_t utime = utime_now();

  int32_t width  = cv_ptr->image.cols; // TODO
  int32_t height =  cv_ptr->image.rows; // TODO

  char const *format = "GRAY8";
  int32_t formatlen = strlen(format);

  int32_t imlen = width*height*sizeof(uint8_t);
  // uint8_t *im = (uint8_t*)malloc(imlen);
  //
  // for (int y = 0; y < height; y++)
  //     for (int x = 0; x < width; x++)
  //         im[y*width+x] = gray.at<uint8_t>(y,x);

  int32_t buflen = 8 + 8 + 4 + 4 + 4 + formatlen + 4 + imlen;
  uint8_t *buf = (uint8_t*)calloc(buflen, sizeof(uint8_t));
  uint8_t *ptr = buf;

  write_i64(ptr, magic);          ptr += 8;
  write_i64(ptr, utime);          ptr += 8;
  write_i32(ptr, width);          ptr += 4;
  write_i32(ptr, height);         ptr += 4;
  write_i32(ptr, formatlen);      ptr += 4;
  memcpy(ptr, format, formatlen); ptr += formatlen;
  write_i32(ptr, imlen);          ptr += 4;
  memcpy(ptr, cv_ptr->image.data, imlen);         ptr += imlen;

  // free(im);

  int bytes = send(sock, buf, buflen, 0);

  free(buf);

  if (bytes != buflen) {
      ROS_ERROR("Tried to send %d bytes, sent %d", len, bytes);
      ros::shutdown();
  }

}

int main(int argc, char *argv[])
{
  ros::init(argc, argv, "camera_to_tcp");

  ros::NodeHandle nh;
  ros::NodeHandle pnh("~");
  image_transport::ImageTransport it(nh);
  image_transport::Subscriber sub = it.subscribe("image", 1, imageCallback);

  std::string host;
  int port;

  pnh.param<std::string>("host", host, "localhost");
  pnh.param<int>("port", port, 7001);

  while(true) { // hack to be able to use break to "return"

    ROS_INFO("Connecting to '%s' on port %d", host.c_str(), port);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock  < 0) {
        ROS_ERROR("Error opening socket");
        break;
    }

    struct hostent *server = gethostbyname(host.c_str());
    if (server == NULL) {
        ROS_ERROR("Error getting host by name");
        break;
    }

    struct sockaddr_in serv_addr;
    bzero((char*) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(port);
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        ROS_ERROR("Error connecting to socket\n");
        break;
    }

    ROS_INFO("Connected");

    ros::Rate r(10.0);
    while (nh.ok()) {
      ros::spinOnce();
      r.sleep();
    }
    break;
    }

    if (sock >= 0)
        close(sock);

    exit(0);
}
