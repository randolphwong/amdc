#include "ros/ros.h"
#include "ros/console.h"

#include "amdc.h"
#include <Eigen/Dense>
#include "controller.h"

using namespace Eigen;
using namespace controller;

// squared distance in which we consider pose is near goal
static const float dist_eps = 0.2;

// conversion factor from force to pwm
static const float force2pwm = 230;

enum sm_states 
{
    sm_idle,
    sm_go2goal,
    sm_find_debris,
    sm_invalid
};

enum sm_states idle();
enum sm_states go2goal();

enum sm_states (*sm_func[2])() =
{
    idle,
    go2goal
};

static enum sm_states state = sm_idle;

enum sm_states idle()
{
    if (amdc_s.goals.size() != 0)
        return sm_go2goal;
    return sm_idle;
}

enum sm_states go2goal()
{
    VectorXf cmd, cmd_pwm;
    cmd = nav_controller(amdc_s.state, amdc_s.goals.front(), amdc_s.range);
    cmd_pwm = u2pwm(cmd);

    // XXX XXX
    ROS_INFO_STREAM("state\n" << amdc_s.state);
    ROS_INFO_STREAM("range\n" << amdc_s.range);
    ROS_INFO_STREAM("ref\n" << amdc_s.goals.front());
    ROS_INFO_STREAM("propeller cmd\n" << cmd);

    amdc_s.propeller_cmd.left_spd = cmd_pwm(0);
    amdc_s.propeller_cmd.right_spd = cmd_pwm(1);
    amdc_s.propeller_cmd.update = true;

    float dist = (amdc_s.goals.front() - amdc_s.state.head(2)).squaredNorm();
    if (dist < dist_eps)
    {
        amdc_s.goals.pop();
        return sm_idle;
    }
    return sm_go2goal;
}

void update_state_machine()
{
    state = sm_func[state]();
}