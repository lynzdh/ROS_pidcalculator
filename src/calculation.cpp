#include "ros/ros.h"
#include "calculation.h"
#include "math.h"

#include <iostream>
using namespace std;

// pid_calc_t -> error
static int get_P(pid_calc_t *pid, pid_parameter_t *pid_param) {
   return pid->error * pid_param->pid_P;
}

// pid_calc_t -> error
// pid_calc_t -> cycle_time
static int get_I(pid_calc_t *pid, pid_parameter_t *pid_param) {
   pid->integrator += (pid->error * pid_param->pid_I) * pid->cycle_time;
   pid->integrator = constrain(pid->integrator, -pid_param->pid_Imax, pid_param->pid_Imax);
   return pid->integrator;
}


// pid_calc_t -> error
// pid_calc_t -> cycle_time
// pid_calc_t -> derivative = pos_vel_t -> cur_vel_raw
static int get_D(pid_calc_t *pid, pid_parameter_t *pid_param) {
   if(pid->cycle_time)
   pid->derivative = (pid->error - pid->last_error) / pid->cycle_time;

// #define DEBUG
#ifdef DEBUG
   cout << " Deriv:" << pid->derivative;
#endif

   // Low pass filter cut frequency for derivative calculation
   // Set to  "1 / ( 2 * PI * gps_lpf )"
#define PID_FILTER       (1.0f / (2.0f * M_PI * (float)50))
   // discrete low pass filter, cuts out the
   // high frequency noise that can drive the controller crazy
   pid->derivative = pid->last_derivative + (pid->cycle_time / (PID_FILTER + pid->cycle_time)) * (pid->derivative - pid->last_derivative);

#ifdef DEBUG
   cout << "  Deriv:" << pid->derivative;
#endif
   // update state

   pid->last_error = pid->error;
   pid->last_derivative = pid->derivative;
   // add in derivative component

   #ifdef DEBUG
   cout << "  Param_D:" << pid_param->pid_D;
   #endif

   return pid_param->pid_D * pid->derivative;
}

void reset_PID(pid_calc_t *pid) {
   pid->integrator = 0;
   pid->last_derivative = 0;
}

double calc_dist(double x, double y, double z, double xx, double yy, double zz) {

   double tmp = 0;
   double sum = 0;
   tmp = (x - xx);
   sum += tmp * tmp;
   tmp = (y - yy);
   sum += tmp * tmp;
   tmp = (z - zz);
   sum += tmp * tmp;

   return sqrt(sum);
}


int constrain(int amt, int low, int high) {
   if (amt < low)
      return low;
   else if (amt > high)
      return high;
   else
      return amt;
}
float constrain(float amt, float low, float high) {
   if (amt < low)
      return low;
   else if (amt > high)
      return high;
   else
      return amt;
}

void calc_pos_error(pid_calc_t *pid, target_pos_vel_t *target, pos_vel_t *current) {
// if (target->target_pos && current->cur_pos )
   pid->error = target->target_pos - current->cur_pos;
   if (current->cycle_time)
      pid->cycle_time = current->cycle_time;
}

void calc_rate_error(pid_calc_t *pid, target_pos_vel_t *target, pos_vel_t *current) {
// if ( target->target_vel && current->cur_vel )
   pid->error = target->target_vel - current->cur_vel;
// if ( current->cur_vel_raw )
   pid->derivative = current->cur_vel_raw;
   if (current->cycle_time)
      pid->cycle_time = current->cycle_time;
}
// pid_calc_t -> error
// pid_calc_t -> cycle_time
// pid_calc_t -> derivative = pos_vel_t -> cur_vel_raw

void calc_pid(pid_calc_t* pid, pid_parameter_t* pid_param) {
   pid->output = pid->inner_p  = get_P(pid, pid_param);
   pid->output += pid->inner_i = get_I(pid, pid_param);
   pid->output += pid->inner_d = constrain( get_D(pid, pid_param), -200 , 200);
}

void calc_velocity( pos_vel_t* pos_vel) {
   const int is_lpf = 1;
   if (pos_vel->last_time && pos_vel->cur_time) {
      pos_vel->cycle_time = pos_vel->cur_time - pos_vel->last_time;

      //if ros's cycle period is fast (in my case, about 1000hz), sometime the cycle period might have some noize over 30%. it must be corrected.
      // if( pos_vel->cycle_time > 0.0013 || pos_vel->cycle_time < 0.0007){
      //         pos_vel->last_vel = pos_vel->cur_vel;
      //         pos_vel->last_time = pos_vel->cur_time;
      //         pos_vel->last_pos = pos_vel->cur_pos;
      //     return;
      // }
   }
   // if (pos_vel->cur_pos && pos_vel->last_pos)
   pos_vel->cur_vel = pos_vel->cur_pos - pos_vel->last_pos;

   if (pos_vel->cycle_time)
      pos_vel->cur_vel /= pos_vel->cycle_time;

   pos_vel->cur_vel_raw = pos_vel->cur_vel;

   // if (pos_vel->last_vel && is_lpf)
   if ( is_lpf )
      pos_vel->cur_vel = ( pos_vel->cur_vel + pos_vel->last_vel) / 2;

   pos_vel->last_vel = pos_vel->cur_vel;
   pos_vel->last_time = pos_vel->cur_time;
   pos_vel->last_pos = pos_vel->cur_pos;
   pos_vel->last_vel_raw = pos_vel->cur_vel_raw;
}



void pos_hold(pid_calc_t *pid_pos_p, pid_calc_t *pid_rate_p, target_pos_vel_t *target_pos_vel_p, pos_vel_t *current, float limited_target_vel, ros::Publisher *pid_inner_pub ) {
   //calculate the target velocity
   calc_pos_error(pid_pos_p, target_pos_vel_p, current);
   // pid_pos_p->output = get_P(pid_pos_p, &pid_pos_param_X);
   calc_pid(pid_pos_p, &pid_pos_param_X);
   target_pos_vel_p->target_vel = pid_pos_p->output;

   target_pos_vel_p->target_vel = constrain(target_pos_vel_p->target_vel, -limited_target_vel, limited_target_vel);
   // (0)target_vel, (1)rateP, (2)rateI, (3)rateD, (4)res

   calc_rate_error(pid_rate_p, target_pos_vel_p, current);
   calc_pid(pid_rate_p, &pid_rate_param_X);


   geometry_msgs::Inertia pid_inner_msg;
   pid_inner_msg.m = target_pos_vel_p->target_vel;
   pid_inner_msg.ixx = pid_rate_p->inner_p;
   pid_inner_msg.ixy = pid_rate_p->inner_i;
   pid_inner_msg.ixz = pid_rate_p->inner_d;
   pid_inner_msg.izz = pid_rate_p->output;
   pid_inner_pub->publish(pid_inner_msg);
}