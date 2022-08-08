#include <player_camera.h>
//VKBP_DISABLE_WARNINGS()
#include <glm/gtx/transform.hpp>
//VKBP_ENABLE_WARNINGS()
#include "rengine.h"

void PlayerCamera::update_camera(float deltaSeconds, int ww, int hh)
{
	w = ww; h = hh;
	const float cam_vel = 0.001f + bSprint * 0.01;
	glm::vec3 forward = { 0,0,cam_vel };
	glm::vec3 right = { cam_vel,0,0 };
	glm::vec3 up = { 0,cam_vel,0 };

	glm::mat4 cam_rot = get_rotation_matrix();

	forward = cam_rot * glm::vec4(forward, 0.f);
	right = cam_rot * glm::vec4(right, 0.f);

	velocity = inputAxis.x * forward + inputAxis.y * right + inputAxis.z * up;

	velocity *= 10 * deltaSeconds;

	position += velocity;
}


glm::mat4 PlayerCamera::get_view_matrix(REngine* engine)
{
	glm::mat4 pre_rotate_mat = glm::mat4(1.0f);
	glm::vec3 rotation_axis = glm::vec3(0.0f, 0.0f, 1.0f);
	//auto a = w / (float)h;
	if (engine->_pretransformFlag & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR) {
		pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::radians(90.0f), rotation_axis);
		//a = h / (float)w;
	}
	else if (engine->_pretransformFlag & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
		pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::radians(270.0f), rotation_axis);
		//a = h / (float)w;
	}
	else if (engine->_pretransformFlag & VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR) {
		pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::radians(180.0f), rotation_axis);
	}

	glm::vec3 camPos = position;
	glm::mat4 cam_rot = get_rotation_matrix();// *pre_rotate_mat;
	glm::mat4 view = glm::translate(glm::mat4{ 1 }, camPos) * cam_rot;

	//we need to invert the camera matrix
	view = glm::inverse(view);

	return view;
}

glm::mat4 PlayerCamera::get_projection_matrix(REngine* engine, bool bReverse)
{
	//glm::mat4 pre_rotate_mat = glm::mat4(1.0f);
	//glm::vec3 rotation_axis = glm::vec3(0.0f, 0.0f, 1.0f);
	auto a = w / (float)h;
	if (engine->_pretransformFlag & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR) {
		//pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::radians(90.0f), rotation_axis);
		a = h / (float)w;
	}
	else if (engine->_pretransformFlag & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
		//pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::radians(270.0f), rotation_axis);
		a = h / (float)w;
	}
	else if (engine->_pretransformFlag & VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR) {
		//pre_rotate_mat = glm::rotate(pre_rotate_mat, glm::radians(180.0f), rotation_axis);
	}

	if (bReverse)
	{
		glm::mat4 pro = glm::perspective(glm::radians(70.f), a, 5000.0f, 0.1f);
		pro[1][1] *= -1;
		return pro;
	}
	else
{
		glm::mat4 pro = glm::perspective(glm::radians(70.f), a, 0.1f, 5000.0f);
		pro[1][1] *= -1;
		return pro;
	}
}

glm::mat4 PlayerCamera::get_rotation_matrix()
{
	glm::mat4 yaw_rot = glm::rotate(glm::mat4{ 1 }, yaw, { 0,-1,0 });
	glm::mat4 pitch_rot = glm::rotate(glm::mat4{ yaw_rot }, pitch, { -1,0,0 });

	return pitch_rot;
}
