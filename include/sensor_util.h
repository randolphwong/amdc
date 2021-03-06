#ifndef sensor_util_H
#define sensor_util_H

#include "amdc.h"

#include "ros/ros.h"
#include "ros/console.h"
#include "amdc/PropellerCmd.h"
#include "sensor_msgs/Range.h"
#include "sensor_msgs/Imu.h"
#include "sensor_msgs/MagneticField.h"
#include "sensor_msgs/NavSatFix.h"
#include "std_msgs/Bool.h"
#include "std_msgs/Int16.h"
#include "std_msgs/Int16MultiArray.h"
#include "std_msgs/Int32MultiArray.h"
#include "std_msgs/Float32MultiArray.h"
#include "geometry_msgs/Vector3.h"
#include <sstream>

const struct
{
    const char *topic;
    const char *frame_id;
} ultrasonic_info[] =
{ {"ultrasonic_btm_left",   "ultrasonic_btm_left_frame"  },
  {"ultrasonic_left",       "ultrasonic_left_frame"      },
  {"ultrasonic_top_left",   "ultrasonic_top_left_frame"  },
  {"ultrasonic_top",        "ultrasonic_top_frame"       },
  {"ultrasonic_top_right",  "ultrasonic_top_right_frame" },
  {"ultrasonic_right",      "ultrasonic_right_frame"     },
  {"ultrasonic_btm_right",  "ultrasonic_btm_right_frame" } };

// TODO
// maybe create a base class?
class ultrasonic_handler
{
    private:
        ros::Subscriber sub;
        ros::Publisher pub;
        sensor_msgs::Range range_msg;
        Amdc *amdc_s;

    public:
        // processed proximity value from sensor (in meters)
        int id;
        float distance;
        int error_code;

        ultrasonic_handler()
        {
            range_msg.radiation_type = sensor_msgs::Range::ULTRASOUND;
            range_msg.field_of_view = 0.2;
            range_msg.min_range = 0.03;
            range_msg.max_range = 6.00;
            distance = 0;
            error_code = 0;
        }

        void callback(const sensor_msgs::Range&);
        void advertise(int id, ros::NodeHandle nh)
        {
            pub = nh.advertise<sensor_msgs::Range>(
                        ultrasonic_info[id].topic,
                        1000);

            range_msg.header.frame_id = ultrasonic_info[id].frame_id;
        }
        void subscribe(int id, ros::NodeHandle nh, Amdc *amdc_handle)
        {
            amdc_s = amdc_handle;
            sub = nh.subscribe(ultrasonic_info[id].topic,
                               1000,
                               &ultrasonic_handler::callback,
                               this);
        }
        void process_sensor_msg(void *buffer);
};

void ultrasonic_handler::callback(const sensor_msgs::Range& msg)
{
    amdc_s->range_raw(id) = msg.range;
    amdc_s->range(id) = amdc_s->kf[id].update(msg.range);
}

void ultrasonic_handler::process_sensor_msg(void *buffer)
{
    unsigned char *buf = (unsigned char *)buffer;
    distance = buf[1];
    distance += buf[2] << 8;
    error_code = buf[3];
    distance /= 100.;
    //distance = 6;
    ROS_DEBUG_STREAM("id " << id << 
                     ", dist: " << distance <<
                     ", error_code: " << error_code);

    // publish as Range msg for visualisation in rviz
    range_msg.header.stamp = ros::Time::now();
    range_msg.range = distance;
    pub.publish(range_msg);
}

// topic name:  "imu_data"    "mag_data" 
// frame id:    "imu_frame"   "mag_frame"
class imu_handler
{
  private:
    ros::Subscriber sub;
    ros::Publisher pub_imu, pub_mag;
    // conversion factor
    // 0.061, 4.35, 6842 are the default factors given in the data sheet
    // imu, p15: https://www.pololu.com/file/0J1087/LSM6DS33.pdf
    // magnetometer, p8: https://www.pololu.com/file/0J1089/LIS3MDL.pdf
    static constexpr double linacc_cf = 0.061 / 1000 * 9.81, 
          angvel_cf = 4.35 * 3.14159265359 / 180 / 1000.,
          magfel_cf = 1. / (10000 * 6842);
    
  public:
    sensor_msgs::Imu imu_msg;
    sensor_msgs::MagneticField mag_msg; 

    imu_handler()
    {
      imu_msg.orientation_covariance = 
        { 0, 0, 0, 
          0, 0, 0, 
          0, 0, 0};
      mag_msg.magnetic_field_covariance = 
        { 0, 0, 0, 
          0, 0, 0, 
          0, 0, 0};
    }

    void callback(const std_msgs::Int16MultiArray::ConstPtr&);

    void advertise(ros::NodeHandle nh)
    {
      pub_imu = nh.advertise<sensor_msgs::Imu>("imu_data", 1000);
      imu_msg.header.frame_id = "imu_frame";

      pub_mag = nh.advertise<sensor_msgs::MagneticField>("mag_data",1000);
      mag_msg.header.frame_id = "imu_frame";
    }

    void subscribe(ros::NodeHandle nh)
    {
      sub = nh.subscribe("im", 1000, &imu_handler::callback, this);
    }
    void process_sensor_msg(void *buffer);
};

void imu_handler::callback(const std_msgs::Int16MultiArray::ConstPtr& msg)
{
  imu_msg.linear_acceleration.x = msg -> data[0] * linacc_cf;
  imu_msg.linear_acceleration.y = msg -> data[1] * linacc_cf;
  imu_msg.linear_acceleration.z = msg -> data[2] * linacc_cf;
  imu_msg.angular_velocity.x = msg -> data[3] * angvel_cf;
  imu_msg.angular_velocity.y = msg -> data[4] * angvel_cf;
  imu_msg.angular_velocity.z = msg -> data[5] * angvel_cf;
  mag_msg.magnetic_field.x = msg -> data[6] * magfel_cf;
  mag_msg.magnetic_field.y = msg -> data[7] * magfel_cf;
  mag_msg.magnetic_field.z = msg -> data[8] * magfel_cf;

  ROS_DEBUG_STREAM("imu,  linear acceleration (m/s2): " 
      << imu_msg.linear_acceleration);
  ROS_DEBUG_STREAM("imu, angular velocities (rad/s): " 
      << imu_msg.angular_velocity); 
  ROS_DEBUG_STREAM("magnetometer, field (Tesla): " 
      << mag_msg.magnetic_field);

  imu_msg.header.stamp = ros::Time::now();
  pub_imu.publish(imu_msg); 

  mag_msg.header.stamp = ros::Time::now();
  pub_mag.publish(mag_msg);
}

void imu_handler::process_sensor_msg(void *buffer)
{
  short *msg = (short *)buffer;
  float x = msg[0] * linacc_cf - 0.925141870975;
  float y = msg[1] * linacc_cf - 0.156783416867;
  if ((x < 0.06) && (x > -0.06)) x = 0;
  if ((y < 0.06) && (y > -0.06)) y = 0;
  imu_msg.linear_acceleration.x = x;
  imu_msg.linear_acceleration.y = y;
  imu_msg.linear_acceleration.z = msg[2] * linacc_cf;
  imu_msg.angular_velocity.x = msg[3] * angvel_cf;
  imu_msg.angular_velocity.y = msg[4] * angvel_cf;
  imu_msg.angular_velocity.z = msg[5] * angvel_cf;
  mag_msg.magnetic_field.x = msg[6] * magfel_cf;
  mag_msg.magnetic_field.y = msg[7] * magfel_cf;
  mag_msg.magnetic_field.z = msg[8] * magfel_cf;

  ROS_DEBUG_STREAM("imu,  linear acceleration (m/s2): " 
      << imu_msg.linear_acceleration);
  ROS_DEBUG_STREAM("imu, angular velocities (rad/s): " 
      << imu_msg.angular_velocity); 
  ROS_DEBUG_STREAM("magnetometer, field (Tesla): " 
      << mag_msg.magnetic_field);

  imu_msg.header.stamp = ros::Time::now();
  imu_msg.header.frame_id = "imu_frame";
  pub_imu.publish(imu_msg); 

  mag_msg.header.stamp = ros::Time::now();
  mag_msg.header.frame_id = "imu_frame";
  pub_mag.publish(mag_msg);
}

class gps_handler
{
  private:
    ros::Subscriber sub;
    ros::Publisher pub; 
    sensor_msgs::NavSatFix gps_msg;

  public:

    gps_handler()
    {
      gps_msg.position_covariance_type = 0; // covariance unkonwn
    }

    void callback(const std_msgs::Int32MultiArray::ConstPtr&); 

    void advertise(ros::NodeHandle nh)
    {
      pub = nh.advertise<sensor_msgs::NavSatFix>("gps_data", 1000);
      gps_msg.header.frame_id = "gps_frame";
    }

    void subscribe(ros::NodeHandle nh)
    {
      sub = nh.subscribe("gp", 1000, &gps_handler::callback, this);
    }
    void process_sensor_msg(void *buffer);
};

void gps_handler::callback(const std_msgs::Int32MultiArray::ConstPtr& msg)
{
  gps_msg.latitude = msg -> data[0] / 10000000.; 
  gps_msg.longitude = msg -> data[1] / 10000000.;
  gps_msg.altitude =  msg -> data[2] / 100.;
  gps_msg.status.status = msg -> data[3];
  gps_msg.status.service = msg -> data[4];

  ROS_DEBUG_STREAM("gps (lat[deg], long[deg], alt[m]), status, service: " 
      << gps_msg.latitude << ", " 
      << gps_msg.longitude << ", "
      << gps_msg.altitude << ", " 
      << gps_msg.status.status << ", "
      << gps_msg.status.service);

  gps_msg.header.stamp = ros::Time::now();
  pub.publish(gps_msg);
}

void gps_handler::process_sensor_msg(void *buffer)
{
  int *msg = (int *)buffer;

  // Only publish legit fix (i.e. when status == 0)
  if (msg[3] != 0) return;

  gps_msg.latitude = msg[0] / 10000000.; 
  gps_msg.longitude = msg[1] / 10000000.;
  gps_msg.altitude =  msg[2] / 100.;
  gps_msg.status.status = msg[3];
  gps_msg.status.service = msg[4];

  ROS_DEBUG_STREAM("gps (lat[deg], long[deg], alt[m]), status, service: " 
      << gps_msg.latitude << ", " 
      << gps_msg.longitude << ", "
      << gps_msg.altitude << ", " 
      << gps_msg.status.status << ", "
      << gps_msg.status.service);

  gps_msg.header.stamp = ros::Time::now();
  gps_msg.header.frame_id = "gps_frame";
  pub.publish(gps_msg);
}

class propeller_handler
{
  private:
    ros::Subscriber sub;
    ros::Publisher pub; 
    int mode;
    int error_code;

  public:
    amdc::PropellerCmd out_msg;
    amdc::PropellerCmd feedback_msg;
    bool update;

    propeller_handler() : update(false)
    {

    }

    void callback(const amdc::PropellerCmd::ConstPtr&); 

    void advertise(ros::NodeHandle nh)
    {
      pub = nh.advertise<amdc::PropellerCmd>("propeller_feedback", 1000);
    }

    void subscribe(ros::NodeHandle nh)
    {
      sub = nh.subscribe("propeller_cmd", 1000, &propeller_handler::callback, this);
    }
    void process_sensor_msg(void *buffer);
};

void propeller_handler::callback(const amdc::PropellerCmd::ConstPtr& msg)
{
  out_msg.left_pwm = msg->left_pwm;
  out_msg.right_pwm = msg->right_pwm;
  out_msg.left_enable = msg->left_enable;
  out_msg.right_enable = msg->right_enable;
  update = true;
}

void propeller_handler::process_sensor_msg(void *buffer)
{
  uint8_t *msg = (uint8_t *) buffer;
  feedback_msg.left_pwm = msg[0];
  feedback_msg.left_pwm += msg[1] << 8;
  feedback_msg.right_pwm = msg[2];
  feedback_msg.right_pwm += msg[3] << 8;
  feedback_msg.left_enable = msg[4];
  feedback_msg.right_enable = msg[5];
  mode = msg[6];
  error_code = msg[7];

  ROS_DEBUG_STREAM("propeller feedback\n"
      << "mode: " << (mode == 0 ? "auto" : "manual") << "\n"
      << "error_code: " << error_code << "\n");

  pub.publish(feedback_msg);
}

class servo_handler
{
  private:
    ros::Subscriber sub;
    ros::Publisher pub;

  public:
    bool open;
    bool update;

    servo_handler() :
      open(false),
      update(false)
    {

    }

    void callback(const std_msgs::Bool::ConstPtr& msg)
    {
        open = msg->data;
        update = true;
    }

    void subscribe(ros::NodeHandle nh)
    {
      sub = nh.subscribe("servo_cmd", 1, &servo_handler::callback, this);
    }
};

#endif
