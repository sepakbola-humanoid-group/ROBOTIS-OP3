/*******************************************************************************
 * Copyright (c) 2016, ROBOTIS CO., LTD.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of ROBOTIS nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************/

/* Author: Kayman Jung */

#include "op3_demo/ball_follower.h"

namespace robotis_op
{

BallFollower::BallFollower()
    : nh_(ros::this_node::getName()),
      FOV_WIDTH(35.2 * M_PI / 180),
      FOV_HEIGHT(21.6 * M_PI / 180),
      count_not_found_(0),
      count_to_kick_(0),
      on_tracking_(false),
      approach_ball_position_(NotFound),
      kick_motion_index_(83),
      NOT_FOUND_THRESHOLD(50),
      MAX_FB_STEP(35.0 * 0.001),
      MAX_RL_TURN(15.0 * M_PI / 180),
      MIN_FB_STEP(5.0 * 0.001),
      MIN_RL_TURN(5.0 * M_PI / 180),
      UNIT_FB_STEP(1.0 * 0.001),
      UNIT_RL_TURN(0.5 * M_PI / 180),
      current_pan_(-10),
      current_tilt_(-10),
      current_x_move_(0.005),
      current_r_angle_(0),
      debug_print_(false)
{
  // module_control_pub_ = nh_.advertise<robotis_controller_msgs::JointCtrlModule>("/robotis/set_joint_ctrl_modules", 0);
  // head_joint_pub_ = nh_.advertise<sensor_msgs::JointState>("/robotis/head_control/set_joint_states_offset", 0);
  // head_scan_pub_ = nh_.advertise<std_msgs::String>("/robotis/head_control/scan_command", 0);
  // motion_index_pub_ = nh_.advertise<std_msgs::Int32>("/robotis/action/page_num", 0);

  //ball_position_sub_ = nh_.subscribe("/ball_detector_node/circle_set", 1, &BallFollower::ballPositionCallback, this);
  //ball_tracking_command_sub_ = nh_.subscribe("/ball_tracker/command", 1, &BallFollower::ballTrackerCommandCallback, this);
  current_joint_states_sub_ = nh_.subscribe("/robotis/goal_joint_states", 10, &BallFollower::currentJointStatesCallback,
                                            this);

  set_walking_command_pub_ = nh_.advertise<std_msgs::String>("/robotis/walking/command", 0);
  set_walking_param_pub_ = nh_.advertise<op3_walking_module_msgs::WalkingParam>("/robotis/walking/set_params", 0);
  get_walking_param_client_ = nh_.serviceClient<op3_walking_module_msgs::GetWalkingParam>(
      "/robotis/walking/get_params");

  //std::string default_path = ros::package::getPath("op3_demo") + "/config/demo_config.yaml";
  //std::string config_path = nh_.param<std::string>("demo_config", default_path);
  //parseJointNameFromYaml(config_path);
}

BallFollower::~BallFollower()
{

}

void BallFollower::startFollowing()
{
  on_tracking_ = true;
  ROS_INFO("Start Ball following");

  setWalkingCommand("start");
}

void BallFollower::stopFollowing()
{
  on_tracking_ = false;
  approach_ball_position_ = NotFound;
  count_to_kick_ = 0;
  accum_ball_position_ = 0;
  ROS_INFO("Stop Ball following");

  setWalkingCommand("stop");
}

void BallFollower::currentJointStatesCallback(const sensor_msgs::JointState::ConstPtr &msg)
{
  double pan, tilt;
  int get_count = 0;

  for (int ix = 0; ix < msg->name.size(); ix++)
  {
    if (msg->name[ix] == "head_pan")
    {
      pan = msg->position[ix];
      get_count += 1;
    }
    else if (msg->name[ix] == "head_tilt")
    {
      tilt = msg->position[ix];
      get_count += 1;
    }

    if (get_count == 2)
      break;
  }

  // check variation
  // if(current_pan_ == -10 || fabs(pan - current_pan_) < 5 * M_PI / 180 )
  // if(current_tilt_ == -10 || fabs(tilt - current_tilt_) < 5 * M_PI / 180 )
  current_pan_ = pan;
  current_tilt_ = tilt;
}

// x_angle : ball position (pan), y_angle : ball position (tilt)
bool BallFollower::processFollowing(double x_angle, double y_angle)
{
  count_not_found_ = 0;
  int ball_position_sum = 0;

  // check of getting head joints angle
  if (current_tilt_ == -10 && current_pan_ == -10)
  {
    ROS_ERROR("Failed to get current angle of head joints.");
    setWalkingCommand("stop");

    on_tracking_ = false;
    approach_ball_position_ = NotFound;
    return false;
  }

  ROS_INFO_COND(debug_print_, "   ============== Head | Ball ==============   ");
  ROS_INFO_STREAM_COND(debug_print_,
                       "== Head Pan : " << (current_pan_ * 180 / M_PI) << " | Ball X : " << (x_angle * 180 / M_PI));
  ROS_INFO_STREAM_COND(debug_print_,
                       "== Head Tilt : " << (current_tilt_ * 180 / M_PI) << " | Ball Y : " << (y_angle * 180 / M_PI));

  approach_ball_position_ = NotFound;

  // clac fb
  //double x_offset = 0.56 * (tan((17 + 70) * M_PI / 180 + current_tilt_) - tan(17 * M_PI / 180));
  //double x_offset = 0.56 * tan(M_PI * 0.5 + current_tilt_ + y_angle - 7 * M_PI / 180);
  double x_offset = 0.56 * tan(M_PI * 0.5 + current_tilt_ - 7 * M_PI / 180);

  double ball_y_angle = (current_tilt_ + y_angle) * 180 / M_PI;
  double ball_x_angle = (current_pan_ + x_angle) * 180 / M_PI;

  if (x_offset < 0)
    x_offset *= (-1);

  //x_offset -= 0.15;

  double fb_goal, fb_move;

  // check whether ball is correct position.
  //if (x_offset < 0.3 && (fabs(current_pan_) < 3 * M_PI / 180))
  if ((ball_y_angle < -63) && (fabs(ball_x_angle) < 30))
  {
    count_to_kick_ += 1;

    ROS_INFO_STREAM("head pan : " << (current_pan_ * 180 / M_PI) << " | ball pan : " << (x_angle * 180 / M_PI));
    ROS_INFO_STREAM("head tilt : " << (current_tilt_ * 180 / M_PI) << " | ball tilt : " << (y_angle * 180 / M_PI));
    ROS_INFO_STREAM("foot to kick : " << accum_ball_position_);

    //if (fabs(x_angle) < 10 * M_PI / 180)
    //{
    if (count_to_kick_ > 15)
    {
      setWalkingCommand("stop");
      on_tracking_ = false;

      // check direction of the ball
      if (accum_ball_position_ > 0)
      {
        ROS_INFO("Ready to kick : left");  // left
        approach_ball_position_ = BallIsLeft;
      }
      else
      {
        ROS_INFO("Ready to kick : right");  // right
        approach_ball_position_ = BallIsRight;
      }

      return true;
    }
    else
    {
      if (ball_x_angle > 0)
        accum_ball_position_ += 1;
      else
        accum_ball_position_ -= 1;

      // send message
      setWalkingParam(MIN_FB_STEP, 0, 0);

      return false;
    }
    //}
  }
  else
  {
    count_to_kick_ = 0;
    accum_ball_position_ = 0;
  }

  fb_goal = fmin(x_offset * 0.1, MAX_FB_STEP);
  if ((x_offset * 0.1) < current_x_move_)
  {
    fb_goal = fmin(current_x_move_ - UNIT_FB_STEP, fb_goal);
    fb_move = fmax(fb_goal, MIN_FB_STEP * 1.2);
  }
  else
  {
    fb_goal = fmin(current_x_move_ + UNIT_FB_STEP, fb_goal);
    fb_move = fmax(fb_goal, MIN_FB_STEP * 1.2);
  }

  // calc rl angle
  //double rl_offset = fabs(current_pan_) * 0.25;
  double rl_offset = fabs(current_pan_ + x_angle) * 0.3;
  double rl_goal, rl_angle;
  rl_goal = fmin(rl_offset, MAX_RL_TURN);
  rl_goal = fmax(rl_goal, MIN_RL_TURN);
  rl_angle = fmin(fabs(current_r_angle_) + UNIT_RL_TURN, rl_goal);

  if (current_pan_ < 0)
    rl_angle *= (-1);

  // send message
  setWalkingParam(fb_move, 0, rl_angle);

  return false;
}

void BallFollower::waitFollowing()
{
  count_not_found_++;

  if (count_not_found_ > NOT_FOUND_THRESHOLD * 0.5)
    setWalkingParam(MIN_FB_STEP, 0, 0);
}

void BallFollower::setWalkingCommand(const std::string &command)
{
  // get param
  if (command == "start")
  {
    getWalkingParam();
    setWalkingParam(0.005, 0, 0, true);
  }

  std_msgs::String _command_msg;
  _command_msg.data = command;
  set_walking_command_pub_.publish(_command_msg);

  ROS_INFO_STREAM("Send Walking command : " << command);
}

void BallFollower::setWalkingParam(double x_move, double y_move, double rotation_angle, bool balance)
{
  current_walking_param_.balance_enable = balance;
  current_walking_param_.x_move_amplitude = x_move;
  current_walking_param_.y_move_amplitude = y_move;
  current_walking_param_.angle_move_amplitude = rotation_angle;

  set_walking_param_pub_.publish(current_walking_param_);
  // ROS_INFO("Change walking param");

  current_x_move_ = x_move;
  current_r_angle_ = rotation_angle;
}

void BallFollower::getWalkingParam()
{
  op3_walking_module_msgs::GetWalkingParam walking_param_msg;

  if (get_walking_param_client_.call(walking_param_msg))
  {
    current_walking_param_ = walking_param_msg.response.parameters;

    // update ui
    ROS_INFO("Get walking parameters");
  }
  else
    ROS_ERROR("Fail to get walking parameters.");
}

}

