#pragma once
#include <glm/glm.hpp>

class REngine;

struct PlayerCamera
{
	glm::vec3 position;
	glm::vec3 velocity;
	glm::vec3 inputAxis;

	float pitch = 0;
	float yaw = 0;
	bool bSprint = false;
	bool bLocked;

	void update_camera(float deltaSeconds);

	glm::mat4 get_view_matrix(REngine* engine);
	glm::mat4 get_projection_matrix(REngine* engine, bool bReverse = true);
	glm::mat4 get_pre_rotation_matrix(REngine* engine);
	glm::mat4 get_rotation_matrix();

	glm::vec3 get_dir();
};