#include <functional>
#include <memory>
#include <mutex>

#include <ros/ros.h>
#include <std_msgs/Bool.h>

#include "phidgets_api/digital_inputs.h"
#include "phidgets_digital_inputs/digital_inputs_ros_i.h"

namespace phidgets {

DigitalInputsRosI::DigitalInputsRosI(ros::NodeHandle nh,
                                     ros::NodeHandle nh_private)
    : nh_(nh), nh_private_(nh_private)
{
    ROS_INFO("Starting Phidgets DigitalInputs");

    int serial_num;
    if (!nh_private_.getParam("serial", serial_num))
    {
        serial_num = -1;  // default open any device
    }
    int hub_port;
    if (!nh_private.getParam("hub_port", hub_port))
    {
        hub_port = 0;  // only used if the device is on a VINT hub_port
    }
    bool is_hub_port_device;
    if (!nh_private.getParam("is_hub_port_device", is_hub_port_device))
    {
        // only used if the device is on a VINT hub_port
        is_hub_port_device = false;
    }
    if (!nh_private.getParam("publish_rate", publish_rate_))
    {
        publish_rate_ = 0;
    }

    ROS_INFO(
        "Waiting for Phidgets DigitalInputs serial %d, hub port %d to be "
        "attached...",
        serial_num, hub_port);

    // We take the mutex here and don't unlock until the end of the constructor
    // to prevent a callback from trying to use the publisher before we are
    // finished setting up.
    std::lock_guard<std::mutex> lock(di_mutex_);

    dis_ = std::make_unique<DigitalInputs>(
        serial_num, hub_port, is_hub_port_device,
        std::bind(&DigitalInputsRosI::stateChangeCallback, this,
                  std::placeholders::_1, std::placeholders::_2));

    int n_in = dis_->getInputCount();
    ROS_INFO("Connected %d inputs", n_in);
    val_to_pubs_.resize(n_in);
    for (int i = 0; i < n_in; i++)
    {
        char topicname[] = "digital_input00";
        snprintf(topicname, sizeof(topicname), "digital_input%02d", i);
        val_to_pubs_[i].pub = nh_.advertise<std_msgs::Bool>(topicname, 1);
        val_to_pubs_[i].last_val = dis_->getInputValue(i);
    }

    if (publish_rate_ > 0)
    {
        timer_ = nh_.createTimer(ros::Duration(1.0 / publish_rate_),
                                 &DigitalInputsRosI::timerCallback, this);
    } else
    {
        // If we are *not* publishing periodically, then we are event driven and
        // will only publish when something changes (where "changes" is defined
        // by the libphidget22 library).  In that case, make sure to publish
        // once at the beginning to make sure there is *some* data.
        for (int i = 0; i < n_in; ++i)
        {
            publishLatest(i);
        }
    }
}

void DigitalInputsRosI::publishLatest(int index)
{
    std_msgs::Bool msg;
    msg.data = val_to_pubs_[index].last_val;
    val_to_pubs_[index].pub.publish(msg);
}

void DigitalInputsRosI::timerCallback(const ros::TimerEvent& /* event */)
{
    std::lock_guard<std::mutex> lock(di_mutex_);
    for (int i = 0; i < static_cast<int>(val_to_pubs_.size()); ++i)
    {
        publishLatest(i);
    }
}

void DigitalInputsRosI::stateChangeCallback(int index, int input_value)
{
    if (static_cast<int>(val_to_pubs_.size()) > index)
    {
        std::lock_guard<std::mutex> lock(di_mutex_);
        val_to_pubs_[index].last_val = input_value == 0;

        if (publish_rate_ <= 0)
        {
            publishLatest(index);
        }
    }
}

}  // namespace phidgets
