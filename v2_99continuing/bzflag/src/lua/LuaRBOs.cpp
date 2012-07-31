/* bzflag
 * Copyright (c) 1993-2010 Tim Riker
 *
 * This package is free software;  you can redistribute it and/or
 * modify it under the terms of the license found in the file
 * named COPYING that should have accompanied this file.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */


#include "common.h"

// interface header
#include "LuaRBOs.h"

// system headers
#include <new>
#include <string>

// common headers
#include "bzfgl.h"
#include "common/bzfio.h"
#include "ogl/OpenGLGState.h"

// local headers
#include "LuaHeader.h"
#include "LuaUtils.h"


const char* LuaRBOMgr::metaName = "RBO";

LuaRBOMgr luaRBOMgr;


//============================================================================//
//============================================================================//

LuaRBO::LuaRBO(const LuaRBOData& rboData)
  : LuaRBOData(rboData) {
  OpenGLGState::registerContextInitializer(StaticFreeContext,
                                           StaticInitContext, this);
}


LuaRBO::~LuaRBO() {
  FreeContext();
  OpenGLGState::unregisterContextInitializer(StaticFreeContext,
                                             StaticInitContext, this);
}



void LuaRBO::Delete() {
  FreeContext();
}


void LuaRBO::InitContext() {
}


void LuaRBO::FreeContext() {
  if (id == 0) {
    return;
  }
  glDeleteRenderbuffers(1, &id);
  id = 0;
}


void LuaRBO::StaticInitContext(void* data) {
  ((LuaRBO*)data)->InitContext();
}


void LuaRBO::StaticFreeContext(void* data) {
  ((LuaRBO*)data)->FreeContext();
}


//============================================================================//
//============================================================================//

LuaRBOMgr::LuaRBOMgr() {
}


LuaRBOMgr::~LuaRBOMgr() {
}


//============================================================================//
//============================================================================//

bool LuaRBOMgr::PushEntries(lua_State* L) {
  CreateMetatable(L);

  PUSH_LUA_CFUNC(L, CreateRBO);
  PUSH_LUA_CFUNC(L, DeleteRBO);

  return true;
}


bool LuaRBOMgr::CreateMetatable(lua_State* L) {
  luaL_newmetatable(L, metaName);
  luaset_strfunc(L,  "__gc",  MetaGC);
  luaset_strfunc(L,  "__index",     MetaIndex);
  luaset_strfunc(L,  "__newindex",  MetaNewindex);
  luaset_strstr(L, "__metatable", "no access");
  lua_pop(L, 1);
  return true;
}


//============================================================================//
//============================================================================//

const LuaRBO* LuaRBOMgr::TestLuaRBO(lua_State* L, int index) {
  if (lua_getuserdataextra(L, index) != metaName) {
    return NULL;
  }
  return (LuaRBO*)lua_touserdata(L, index);
}


const LuaRBO* LuaRBOMgr::CheckLuaRBO(lua_State* L, int index) {
  if (lua_getuserdataextra(L, index) != metaName) {
    luaL_argerror(L, index, "expected RBO");
  }
  return (LuaRBO*)lua_touserdata(L, index);
}


LuaRBO* LuaRBOMgr::GetLuaRBO(lua_State* L, int index) {
  if (lua_getuserdataextra(L, index) != metaName) {
    luaL_argerror(L, index, "expected RBO");
  }
  return (LuaRBO*)lua_touserdata(L, index);
}


//============================================================================//
//============================================================================//

int LuaRBOMgr::MetaGC(lua_State* L) {
  LuaRBO* rbo = GetLuaRBO(L, 1);
  rbo->~LuaRBO();
  return 0;
}


int LuaRBOMgr::MetaIndex(lua_State* L) {
  const LuaRBO* rbo = CheckLuaRBO(L, 1);
  if (!lua_israwstring(L, 2)) {
    return luaL_pushnil(L);
  }
  const std::string key = luaL_checkstring(L, 2);
  if (key == "valid") {
    lua_pushboolean(L, glIsRenderbuffer(rbo->id));
  }
  else if (key == "target") { lua_pushinteger(L, rbo->target); }
  else if (key == "format") { lua_pushinteger(L, rbo->format); }
  else if (key == "xsize")  { lua_pushinteger(L, rbo->xsize);  }
  else if (key == "ysize")  { lua_pushinteger(L, rbo->ysize);  }
  else {
    return luaL_pushnil(L);
  }
  return 1;
}


int LuaRBOMgr::MetaNewindex(lua_State* /*L*/) {
  return 0;
}


//============================================================================//
//============================================================================//

int LuaRBOMgr::CreateRBO(lua_State* L) {
  LuaRBOData rboData;

  rboData.xsize = (GLsizei)luaL_checknumber(L, 1);
  rboData.ysize = (GLsizei)luaL_checknumber(L, 2);
  rboData.target = GL_RENDERBUFFER;
  rboData.format = GL_RGBA;

  const int table = 3;
  if (lua_istable(L, table)) {
    lua_getfield(L, table, "target");
    if (lua_israwnumber(L, -1)) {
      rboData.target = (GLenum)lua_toint(L, -1);
    }
    lua_pop(L, 1);
    lua_getfield(L, table, "format");
    if (lua_israwnumber(L, -1)) {
      rboData.format = (GLenum)lua_toint(L, -1);
    }
    lua_pop(L, 1);
  }

  glGenRenderbuffers(1, &rboData.id);
  glBindRenderbuffer(rboData.target, rboData.id);

  // allocate the memory
  glRenderbufferStorage(rboData.target, rboData.format,
                        rboData.xsize,  rboData.ysize);

  glBindRenderbuffer(rboData.target, 0);

  void* udData = lua_newuserdata(L, sizeof(LuaRBO));
  new(udData) LuaRBO(rboData);

  lua_setuserdataextra(L, -1, (void*)metaName);
  luaL_getmetatable(L, metaName);
  lua_setmetatable(L, -2);

  return 1;
}


int LuaRBOMgr::DeleteRBO(lua_State* L) {
  if (OpenGLGState::isExecutingInitFuncs()) {
    luaL_error(L, "gl.DeleteRBO can not be used in GLReload");
  }
  if (lua_isnil(L, 1)) {
    return 0;
  }
  LuaRBO* rbo = GetLuaRBO(L, 1);
  rbo->Delete();
  return 0;
}


//============================================================================//
//============================================================================//


// Local Variables: ***
// mode: C++ ***
// tab-width: 8 ***
// c-basic-offset: 2 ***
// indent-tabs-mode: nil ***
// End: ***
// ex: shiftwidth=2 tabstop=8 expandtab