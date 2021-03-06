/****************************************************************************
 *
 *   Copyright (c) 2018 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file CollisionPrevention.hpp
 * @author Tanja Baumann <tanja@auterion.com>
 *
 * CollisionPrevention controller.
 *
 */

#pragma once

#include <float.h>

#include <commander/px4_custom_mode.h>
#include <drivers/drv_hrt.h>
#include <mathlib/mathlib.h>
#include <matrix/matrix/math.hpp>
#include <px4_module_params.h>
#include <systemlib/mavlink_log.h>
#include <uORB/Publication.hpp>
#include <uORB/PublicationQueued.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/topics/collision_constraints.h>
#include <uORB/topics/distance_sensor.h>
#include <uORB/topics/mavlink_log.h>
#include <uORB/topics/obstacle_distance.h>
#include <uORB/topics/vehicle_attitude.h>
#include <uORB/topics/vehicle_command.h>

class CollisionPrevention : public ModuleParams
{
public:
	CollisionPrevention(ModuleParams *parent);

	~CollisionPrevention();
	/**
	 * Returs true if Collision Prevention is running
	 */
	bool is_active() { return _param_mpc_col_prev_d.get() > 0; }

	/**
	 * Computes collision free setpoints
	 * @param original_setpoint, setpoint before collision prevention intervention
	 * @param max_speed, maximum xy speed
	 * @param curr_pos, current vehicle position
	 * @param curr_vel, current vehicle velocity
	 */
	void modifySetpoint(matrix::Vector2f &original_setpoint, const float max_speed,
			    const matrix::Vector2f &curr_pos, const matrix::Vector2f &curr_vel);

private:

	bool _interfering{false};		/**< states if the collision prevention interferes with the user input */

	orb_advert_t _mavlink_log_pub{nullptr};	 	/**< Mavlink log uORB handle */

	uORB::Publication<collision_constraints_s>	_constraints_pub{ORB_ID(collision_constraints)};		/**< constraints publication */
	uORB::Publication<obstacle_distance_s>		_obstacle_distance_pub{ORB_ID(obstacle_distance_fused)};	/**< obstacle_distance publication */
	uORB::PublicationQueued<vehicle_command_s>	_vehicle_command_pub{ORB_ID(vehicle_command)};			/**< vehicle command do publication */

	uORB::SubscriptionData<obstacle_distance_s> _sub_obstacle_distance{ORB_ID(obstacle_distance)}; /**< obstacle distances received form a range sensor */
	uORB::Subscription _sub_distance_sensor[ORB_MULTI_MAX_INSTANCES] {{ORB_ID(distance_sensor), 0}, {ORB_ID(distance_sensor), 1}, {ORB_ID(distance_sensor), 2}, {ORB_ID(distance_sensor), 3}}; /**< distance data received from onboard rangefinders */
	uORB::SubscriptionData<vehicle_attitude_s> _sub_vehicle_attitude{ORB_ID(vehicle_attitude)};

	static constexpr uint64_t RANGE_STREAM_TIMEOUT_US{500000};

	DEFINE_PARAMETERS(
		(ParamFloat<px4::params::MPC_COL_PREV_D>) _param_mpc_col_prev_d, /**< collision prevention keep minimum distance */
		(ParamFloat<px4::params::MPC_COL_PREV_ANG>) _param_mpc_col_prev_ang, /**< collision prevention detection angle */
		(ParamFloat<px4::params::MPC_XY_P>) _param_mpc_xy_p, /**< p gain from position controller*/
		(ParamFloat<px4::params::MPC_COL_PREV_DLY>) _param_mpc_col_prev_dly, /**< delay of the range measurement data*/
		(ParamFloat<px4::params::MPC_JERK_MAX>) _param_mpc_jerk_max, /**< vehicle maximum jerk*/
		(ParamFloat<px4::params::MPC_ACC_HOR>) _param_mpc_acc_hor /**< vehicle maximum horizontal acceleration*/
	)

	/**
	 * Transforms the sensor orientation into a yaw in the local frame
	 * @param distance_sensor, distance sensor message
	 * @param angle_offset, sensor body frame offset
	 */
	inline float _sensorOrientationToYawOffset(const distance_sensor_s &distance_sensor, float angle_offset)
	{

		float offset = angle_offset > 0.f ? math::radians(angle_offset) : 0.0f;

		switch (distance_sensor.orientation) {
		case distance_sensor_s::ROTATION_RIGHT_FACING:
			offset = M_PI_F / 2.0f;
			break;

		case distance_sensor_s::ROTATION_LEFT_FACING:
			offset = -M_PI_F / 2.0f;
			break;

		case distance_sensor_s::ROTATION_BACKWARD_FACING:
			offset = M_PI_F;
			break;

		case distance_sensor_s::ROTATION_CUSTOM:
			offset = matrix::Eulerf(matrix::Quatf(distance_sensor.q)).psi();
			break;
		}

		return offset;
	}

	/**
	 * Computes collision free setpoints
	 * @param setpoint, setpoint before collision prevention intervention
	 * @param curr_pos, current vehicle position
	 * @param curr_vel, current vehicle velocity
	 */
	void _calculateConstrainedSetpoint(matrix::Vector2f &setpoint, const matrix::Vector2f &curr_pos,
					   const matrix::Vector2f &curr_vel);

	/**
	 * Updates obstacle distance message with measurement from offboard
	 * @param obstacle, obstacle_distance message to be updated
	 */
	void _updateOffboardObstacleDistance(obstacle_distance_s &obstacle);

	/**
	 * Updates obstacle distance message with measurement from distance sensors
	 * @param obstacle, obstacle_distance message to be updated
	 */
	void _updateDistanceSensor(obstacle_distance_s &obstacle);

	/**
	 * Publishes vehicle command.
	 */
	void _publishVehicleCmdDoLoiter();

};
