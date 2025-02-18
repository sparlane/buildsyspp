/******************************************************************************
 Copyright 2013 Allied Telesis Labs Ltd. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#include "include/buildsys.h"
#include "interface/builddir.hpp"
#include "interface/luainterface.h"
#include <sys/stat.h>

static int li_name(lua_State *L)
{
	if(lua_gettop(L) != 0) {
		throw CustomException("name() takes no arguments");
	}

	Package *P = li_get_package();

	std::string value = P->getNS()->getName();
	lua_pushstring(L, value.c_str());
	return 1;
}

static int li_package_name(lua_State *L)
{
	if(lua_gettop(L) != 0) {
		throw CustomException("package_name() takes no arguments");
	}

	Package *P = li_get_package();

	const std::string &value = P->getName();
	lua_pushstring(L, value.c_str());
	return 1;
}

static std::string get_feature_value(const std::string &package_name,
                                     const std::string &key)
{
	/* Allow the NoKeyException to fall-thru if all possible package namespaces have been
	 * tried */
	if(package_name.empty()) {
		return li_get_feature_map()->getFeature(key);
	}
	/* Try the feature prefixed with our package name */
	try {
		return li_get_feature_map()->getFeature(package_name + ":" + key);
	} catch(NoKeyException &E) {
		/* Try the parent package directory name(s) too */
		std::size_t found = package_name.find_last_of('/');
		if(found == std::string::npos) {
			return get_feature_value("", key);
		}
		return get_feature_value(package_name.substr(0, found), key);
	}
}

static int li_feature(lua_State *L)
{
	if(lua_gettop(L) < 1 || lua_gettop(L) > 3) {
		throw CustomException("feature() takes 1 to 3 arguments");
	}
	if(lua_type(L, 1) != LUA_TSTRING) {
		throw CustomException("First argument to feature() must be a string");
	}

	Package *P = li_get_package();
	std::string key(lua_tostring(L, 1));

	if(lua_gettop(L) == 1) {
		try {
			std::string value = get_feature_value(P->getName(), key);
			lua_pushstring(L, value.c_str());
			P->buildDescription()->add_feature_value(key, value);
		} catch(NoKeyException &E) {
			lua_pushnil(L);
			P->buildDescription()->add_nil_feature_value(key);
		}
		return 1;
	}

	if(lua_type(L, 2) != LUA_TSTRING) {
		throw CustomException("Second argument to feature() must be a string");
	}
	std::string value(lua_tostring(L, 2));
	bool override = false;

	if(lua_gettop(L) == 3) {
		if(lua_type(L, 3) != LUA_TBOOLEAN) {
			throw CustomException(
			    "Third argument to feature() must be boolean, if present");
		}
		override = (lua_toboolean(L, -3) != 0);
	}

	li_get_feature_map()->setFeature(key, value, override);
	return 0;
}

static int li_builddir(lua_State *L)
{
	bool clean_requested = false;
	int args = lua_gettop(L);
	if(args > 1) {
		throw CustomException("builddir() takes 1 or no arguments");
	}

	if(args == 1) {
		if(!lua_isboolean(L, 1)) {
			throw CustomException(
			    "builddir() expects a boolean as the first argument, if present");
		}
		clean_requested = (lua_toboolean(L, 1) != 0);
	}

	Package *P = li_get_package();

	// create a table, since this is actually an object
	CREATE_TABLE(L, P->builddir());
	li_builddir_create(L, P->builddir());

	if(clean_requested) {
		P->set_clean_before_build();
	}

	return 1;
}

static int li_intercept(lua_State *L)
{
	bool install = false;
	bool staging = false;

	if(lua_gettop(L) == 0) {
		/* Default behaviour is to only intercept the install directories */
		staging = false;
		install = true;
	} else if(lua_gettop(L) == 1) {
		if(lua_istable(L, 1)) {
			lua_pushnil(L); /* first key */
			while(lua_next(L, 1) != 0) {
				/* uses 'key' (at index -2) and 'value' (at index -1) */
				if(lua_type(L, -2) != LUA_TSTRING) {
					throw CustomException(
					    "intercept() requires a table with strings as keys\n");
				}
				std::string key(lua_tostring(L, -2));
				if(key == "staging") {
					if(lua_type(L, -1) == LUA_TBOOLEAN) {
						staging = (lua_toboolean(L, -1) != 0);
					} else {
						throw CustomException("intercept() requires a boolean argument to "
						                      "the staging parameter");
					}
				} else if(key == "install") {
					if(lua_type(L, -1) == LUA_TBOOLEAN) {
						install = (lua_toboolean(L, -1) != 0);
					} else {
						throw CustomException("intercept() requires a boolean argument to "
						                      "the install parameter");
					}
				}
				/* removes 'value'; keeps 'key' for next iteration */
				lua_pop(L, 1);
			}
		} else {
			throw CustomException(
			    "intercept() requires a table as the first argument if present");
		}
	} else {
		throw CustomException("intercept() takes no or 1 argument(s)");
	}

	Package *P = li_get_package();

	P->setIntercept(install, staging);
	return 0;
}

static int li_keepstaging(lua_State *L)
{
	if(lua_gettop(L) != 0) {
		throw CustomException("keepstaging() takes no arguments");
	}

	Package *P = li_get_package();

	P->setSuppressRemoveStaging(true);
	return 0;
}

static void depend(Package *P, NameSpace *ns, bool locally, const std::string &name)
{
	Package *p = nullptr;
	// create the Package
	if(ns != nullptr) {
		p = ns->findPackage(name);
	} else {
		p = P->getNS()->findPackage(name);
	}
	if(p == nullptr) {
		throw CustomException("Failed to create or find Package");
	}

	P->depend(p, locally);
}

int li_depend(lua_State *L)
{
	if(!(lua_gettop(L) == 1 || lua_gettop(L) == 2)) {
		throw CustomException("depend() takes 1 argument or 2 arguments");
	}

	NameSpace *ns = nullptr;
	// get the current package
	Package *P = li_get_package();

	if(lua_type(L, 1) == LUA_TSTRING) {
		if(lua_gettop(L) == 2) {
			if(lua_type(L, 2) != LUA_TSTRING) {
				throw CustomException("depend() takes a string as the second argument");
			}
			ns = NameSpace::findNameSpace(std::string(lua_tostring(L, 2)));
		}
		depend(P, ns, false, std::string(lua_tostring(L, 1)));
	} else if(lua_istable(L, 1)) {
		std::list<std::string> package_names;
		bool locally = false;
		lua_pushnil(L); /* first key */
		while(lua_next(L, 1) != 0) {
			/* uses 'key' (at index -2) and 'value' (at index -1) */
			if(lua_type(L, -2) != LUA_TSTRING) {
				throw CustomException("depend() requires a table with strings as keys\n");
			}
			std::string key(lua_tostring(L, -2));
			if(key == "package" || key == "packages") {
				if(lua_type(L, -1) == LUA_TSTRING) {
					package_names.emplace_back(lua_tostring(L, -1));
				} else if(lua_istable(L, -1)) {
					lua_pushnil(L);
					while(lua_next(L, -2) != 0) {
						if(lua_type(L, -1) != LUA_TSTRING) {
							throw CustomException("depend() requires a single package "
							                      "name or table of package names\n");
						}
						package_names.emplace_back(lua_tostring(L, -1));
						lua_pop(L, 1);
					}
				} else {
					throw CustomException("depend() requires a single package name or "
					                      "table of package names\n");
				}
			} else if(key == "namespace") {
				if(lua_type(L, -1) != LUA_TSTRING) {
					throw CustomException(
					    "depend() requires a string for the namespace name\n");
				}
				ns = NameSpace::findNameSpace(std::string(lua_tostring(L, -1)));
			} else if(key == "locally") {
				if(lua_type(L, -1) == LUA_TBOOLEAN) {
					locally = (lua_toboolean(L, -1) != 0);
				} else if(lua_type(L, -1) == LUA_TSTRING) {
					std::string value(lua_tostring(L, -1));
					if(value == "true") {
						locally = true;
					}
				}
			}
			/* removes 'value'; keeps 'key' for next iteration */
			lua_pop(L, 1);
		}
		for(auto &package_name : package_names) {
			depend(P, ns, locally, package_name);
		}
	} else {
		throw CustomException("depend() takes a string or a table of strings");
	}

	return 0;
}

static int li_hashoutput(lua_State *L)
{
	if(lua_gettop(L) != 0) {
		throw CustomException("hashoutput() takes no arguments");
	}

	Package *P = li_get_package();

	// Instead of depender using build.info hash
	// create an output.info and get them to hash that
	// useful for kernel-headers and other packages
	// that produce data that changes less often
	// than the sources
	P->setHashOutput(true);
	return 0;
}

static int li_require(lua_State *L)
{
	if(lua_gettop(L) != 1) {
		throw CustomException("require() takes 1 argument");
	}

	if(lua_isstring(L, 1) == 0) {
		throw CustomException("Argument to require() must be a string");
	}

	Package *P = li_get_package();

	std::string fname = std::string(lua_tostring(L, 1)) + ".lua";
	std::string relative_fname = P->relative_fetch_path(fname, true);

	// Check whether the relative file path exists. If it does
	// not exist then we use the original file path
	if(!filesystem::exists(relative_fname)) {
		throw FileNotFoundException("require", fname);
	}

	int ret_values = P->getLua()->processFile(relative_fname);
	P->buildDescription()->add_require_file(fname, hash_file(relative_fname));

	return ret_values;
}

static int li_optionally_require(lua_State *L)
{
	if(lua_gettop(L) != 1) {
		throw CustomException("optionally_require() takes 1 argument");
	}

	if(lua_isstring(L, 1) == 0) {
		throw CustomException("Argument to optionally_require() must be a string");
	}

	Package *P = li_get_package();

	std::string fname = std::string(lua_tostring(L, 1)) + ".lua";
	std::string relative_fname;

	try {
		relative_fname = P->relative_fetch_path(fname, true);
	} catch(FileNotFoundException &fnf) {
		/* This action was optional, so we don't care */
		return 0;
	}

	int ret_values = P->getLua()->processFile(relative_fname);
	P->buildDescription()->add_require_file(fname, hash_file(relative_fname));

	return ret_values;
}

static int li_overlay_add(lua_State *L)
{
	if(lua_gettop(L) != 1) {
		throw CustomException("overlayadd() takes 1 argument");
	}

	if(lua_isstring(L, 1) == 0) {
		throw CustomException("Argument to overlayadd() must be a string");
	}

	std::string overlay_path = std::string(lua_tostring(L, 1));
	Package::add_overlay_path(overlay_path, true);

	return 0;
}

bool buildsys::interfaceSetup(Lua *lua)
{
	lua->registerFunc("builddir", li_builddir);
	lua->registerFunc("depend", li_depend);
	lua->registerFunc("feature", li_feature);
	lua->registerFunc("intercept", li_intercept);
	lua->registerFunc("keepstaging", li_keepstaging);
	lua->registerFunc("name", li_name);
	lua->registerFunc("package_name", li_package_name);
	lua->registerFunc("hashoutput", li_hashoutput);
	lua->registerFunc("require", li_require);
	lua->registerFunc("optionally_require", li_optionally_require);
	lua->registerFunc("overlayadd", li_overlay_add);

	return true;
}
