#ifndef RT_INCLUDES_H
#define RT_INCLUDES_H

#include <vector>
#include <iostream>
#include <string>

#include "includes/shader.h"
#include "includes/camera.h"
#include "rt_structs.h"
#include "rt_mesh.h"
#include "rt_bvh.h"
#include "rt_skybox.h"
#include "rt_input.h"

inline double random_double() {
	// Returns a random real in [0,1).
	return std::rand() / (RAND_MAX + 1.0);
}


#endif // !RT_INCLUDES_H

