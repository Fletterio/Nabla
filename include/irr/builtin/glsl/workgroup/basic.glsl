#ifndef _IRR_BUILTIN_GLSL_WORKGROUP_BASIC_INCLUDED_
#define _IRR_BUILTIN_GLSL_WORKGROUP_BASIC_INCLUDED_

#include <irr/builtin/glsl/macros.glsl>
#include <irr/builtin/glsl/math/typeless_arithmetic.glsl>


#ifndef _IRR_GLSL_WORKGROUP_SIZE_
	#error "User needs to let us know the size of the workgroup via _IRR_GLSL_WORKGROUP_SIZE_!"
#endif
		

//! all functions must be called in uniform control flow (all workgroup invocations active)
bool irr_glsl_workgroupElect()
{
	return gl_LocalInvocationIndex==0u;
}


#endif
