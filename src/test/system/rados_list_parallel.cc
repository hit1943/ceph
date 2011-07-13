// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
* Ceph - scalable distributed file system
*
* Copyright (C) 2011 New Dream Network
*
* This is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License version 2.1, as published by the Free Software
* Foundation.  See file COPYING.
*
*/

#include "cross_process_sem.h"
#include "include/rados/librados.h"
#include "st_rados_create_pool.h"
#include "systest_runnable.h"
#include "systest_settings.h"

#include <errno.h>
#include <map>
#include <pthread.h>
#include <semaphore.h>
#include <sstream>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <time.h>
#include <vector>

using std::ostringstream;
using std::string;
using std::vector;

static int g_num_objects = 50;

static CrossProcessSem *pool_setup_sem = NULL;
static CrossProcessSem *modify_sem = NULL;

class RadosListObjectsR : public SysTestRunnable
{
public:
  RadosListObjectsR(int argc, const char **argv)
    : SysTestRunnable(argc, argv)
  {
  }

  ~RadosListObjectsR()
  {
  }

  int run()
  {
    rados_t cl;
    RETURN_IF_NONZERO(rados_create(&cl, NULL));
    rados_conf_parse_argv(cl, m_argc, m_argv);
    RETURN_IF_NONZERO(rados_conf_read_file(cl, NULL));
    RETURN_IF_NONZERO(rados_connect(cl));
    pool_setup_sem->wait();
    pool_setup_sem->post();

    rados_ioctx_t io_ctx;
    RETURN_IF_NOT_VAL(-EEXIST, rados_pool_create(cl, "foo"));
    RETURN_IF_NONZERO(rados_ioctx_create(cl, "foo", &io_ctx));

    int ret, saw = 0;
    const char *obj_name;
    rados_list_ctx_t h;
    printf("%s: listing objects.\n", get_id_str());
    RETURN_IF_NONZERO(rados_objects_list_open(io_ctx, &h));
    while (true) {
      ret = rados_objects_list_next(h, &obj_name);
      if (ret == -ENOENT) {
	break;
      }
      else if (ret != 0) {
	printf("%s: rados_objects_list_next error: %d\n", get_id_str(), ret);
	return ret;
      }
      char *obj_name_copy = strdup(obj_name);
      free(obj_name_copy);
      if ((saw % 25) == 0) {
	printf("%s: listed object %d...\n", get_id_str(), saw);
      }
      ++saw;
      if (saw == g_num_objects / 2)
	modify_sem->wait();
    }
    rados_objects_list_close(h);

    printf("%s: saw %d objects\n", get_id_str(), saw);

    rados_ioctx_destroy(io_ctx);
    rados_shutdown(cl);

    return 0;
  }
};

class RadosDeleteObjectsR : public SysTestRunnable
{
public:
  RadosDeleteObjectsR(int argc, const char **argv)
    : SysTestRunnable(argc, argv)
  {
  }

  ~RadosDeleteObjectsR()
  {
  }

  int run(void)
  {
    int ret;
    rados_t cl;
    RETURN_IF_NONZERO(rados_create(&cl, NULL));
    rados_conf_parse_argv(cl, m_argc, m_argv);
    RETURN_IF_NONZERO(rados_conf_read_file(cl, NULL));
    std::string log_name = SysTestSettings::inst().get_log_name(get_id_str());
    if (!log_name.empty())
      rados_conf_set(cl, "log_file", log_name.c_str());
    RETURN_IF_NONZERO(rados_connect(cl));
    pool_setup_sem->wait();
    pool_setup_sem->post();

    rados_ioctx_t io_ctx;
    RETURN_IF_NOT_VAL(-EEXIST, rados_pool_create(cl, "foo"));
    RETURN_IF_NONZERO(rados_ioctx_create(cl, "foo", &io_ctx));

    std::map <int, std::string> to_delete;
    for (int i = 0; i < g_num_objects; ++i) {
      char oid[128];
      snprintf(oid, sizeof(oid), "%d.obj", i);
      to_delete[i] = oid;
    }

    int removed = 0;
    while (true) {
      if (to_delete.empty())
	break;
      int r = rand() % to_delete.size();
      std::map <int, std::string>::iterator d = to_delete.begin();
      for (int i = 0; i < r; ++i)
	++d;
      if (d == to_delete.end()) {
	return -EDOM;
      }
      std::string oid(d->second);
      to_delete.erase(d);
      ret = rados_remove(io_ctx, oid.c_str());
      if (ret != 0) {
	printf("%s: rados_remove(%s) failed with error %d\n",
	       get_id_str(), oid.c_str(), ret);
	return ret;
      }
      ++removed;
      if ((removed % 25) == 0) {
	printf("%s: removed %d objects...\n", get_id_str(), removed);
      }
      if (removed == g_num_objects / 2) {
	printf("%s: removed half of the objects\n", get_id_str());
	modify_sem->post();
      }
    }

    printf("%s: removed %d objects\n", get_id_str(), removed);

    rados_ioctx_destroy(io_ctx);
    rados_shutdown(cl);

    return 0;
  }
};

class RadosAddObjectsR : public SysTestRunnable
{
public:
  RadosAddObjectsR(int argc, const char **argv, const std::string &suffix)
    : SysTestRunnable(argc, argv),
      m_suffix(suffix)
  {
  }

  ~RadosAddObjectsR()
  {
  }

  int run(void)
  {
    int ret;
    rados_t cl;
    RETURN_IF_NONZERO(rados_create(&cl, NULL));
    rados_conf_parse_argv(cl, m_argc, m_argv);
    RETURN_IF_NONZERO(rados_conf_read_file(cl, NULL));
    std::string log_name = SysTestSettings::inst().get_log_name(get_id_str());
    if (!log_name.empty())
      rados_conf_set(cl, "log_file", log_name.c_str());
    RETURN_IF_NONZERO(rados_connect(cl));
    pool_setup_sem->wait();
    pool_setup_sem->post();

    rados_ioctx_t io_ctx;
    RETURN_IF_NOT_VAL(-EEXIST, rados_pool_create(cl, "foo"));
    RETURN_IF_NONZERO(rados_ioctx_create(cl, "foo", &io_ctx));

    std::map <int, std::string> to_add;
    for (int i = 0; i < g_num_objects; ++i) {
      char oid[128];
      snprintf(oid, sizeof(oid), "%d.%s", i, m_suffix.c_str());
      to_add[i] = oid;
    }

    int added = 0;
    while (true) {
      if (to_add.empty())
	break;
      int r = rand() % to_add.size();
      std::map <int, std::string>::iterator d = to_add.begin();
      for (int i = 0; i < r; ++i)
	++d;
      if (d == to_add.end()) {
	return -EDOM;
      }
      std::string oid(d->second);
      to_add.erase(d);

      std::string buf(StRadosCreatePool::get_random_buf(256));
      ret = rados_write(io_ctx, oid.c_str(), buf.c_str(), buf.size(), 0);
      if (ret != (int)buf.size()) {
	printf("%s: rados_write(%s) failed with error %d\n",
	       get_id_str(), oid.c_str(), ret);
	return ret;
      }
      ++added;
      if ((added % 25) == 0) {
	printf("%s: added %d objects...\n", get_id_str(), added);
      }
      if (added == g_num_objects / 2) {
	printf("%s: added half of the objects\n", get_id_str());
	modify_sem->post();
      }
    }

    printf("%s: added %d objects\n", get_id_str(), added);

    rados_ioctx_destroy(io_ctx);
    rados_shutdown(cl);

    return 0;
  }
private:
  std::string m_suffix;
};

const char *get_id_str()
{
  return "main";
}

int main(int argc, const char **argv)
{
  const char *num_objects = getenv("NUM_OBJECTS");
  if (num_objects) {
    g_num_objects = atoi(num_objects); 
    if (g_num_objects == 0)
      return 100;
  }

  RETURN_IF_NONZERO(CrossProcessSem::create(0, &pool_setup_sem));
  RETURN_IF_NONZERO(CrossProcessSem::create(1, &modify_sem));

  std::string error;

  // Test 1... list objects
  {
    StRadosCreatePool r1(argc, argv, pool_setup_sem, NULL, g_num_objects);
    RadosListObjectsR r2(argc, argv);
    vector < SysTestRunnable* > vec;
    vec.push_back(&r1);
    vec.push_back(&r2);
    error = SysTestRunnable::run_until_finished(vec);
    if (!error.empty()) {
      printf("got error: %s\n", error.c_str());
      return EXIT_FAILURE;
    }
  }

  // Test 2... list objects while they're being deleted
  RETURN_IF_NONZERO(pool_setup_sem->reinit(0));
  RETURN_IF_NONZERO(modify_sem->reinit(0));
  {
    StRadosCreatePool r1(argc, argv, pool_setup_sem, NULL, g_num_objects);
    RadosListObjectsR r2(argc, argv);
    RadosDeleteObjectsR r3(argc, argv);
    vector < SysTestRunnable* > vec;
    vec.push_back(&r1);
    vec.push_back(&r2);
    vec.push_back(&r3);
    error = SysTestRunnable::run_until_finished(vec);
    if (!error.empty()) {
      printf("got error: %s\n", error.c_str());
      return EXIT_FAILURE;
    }
  }

  // Test 3... list objects while others are being added
  RETURN_IF_NONZERO(pool_setup_sem->reinit(0));
  RETURN_IF_NONZERO(modify_sem->reinit(0));
  {
    StRadosCreatePool r1(argc, argv, pool_setup_sem, NULL, g_num_objects);
    RadosListObjectsR r2(argc, argv);
    RadosAddObjectsR r3(argc, argv, "obj2");
    vector < SysTestRunnable* > vec;
    vec.push_back(&r1);
    vec.push_back(&r2);
    vec.push_back(&r3);
    error = SysTestRunnable::run_until_finished(vec);
    if (!error.empty()) {
      printf("got error: %s\n", error.c_str());
      return EXIT_FAILURE;
    }
  }

  // Test 4... list objects while others are being added and deleted
  RETURN_IF_NONZERO(pool_setup_sem->reinit(0));
  RETURN_IF_NONZERO(modify_sem->reinit(0));
  {
    StRadosCreatePool r1(argc, argv, pool_setup_sem, NULL, g_num_objects);
    RadosListObjectsR r2(argc, argv);
    RadosAddObjectsR r3(argc, argv, "obj2");
    RadosAddObjectsR r4(argc, argv, "obj3");
    RadosDeleteObjectsR r5(argc, argv);
    vector < SysTestRunnable* > vec;
    vec.push_back(&r1);
    vec.push_back(&r2);
    vec.push_back(&r3);
    vec.push_back(&r4);
    vec.push_back(&r5);
    error = SysTestRunnable::run_until_finished(vec);
    if (!error.empty()) {
      printf("got error: %s\n", error.c_str());
      return EXIT_FAILURE;
    }
  }

  printf("******* SUCCESS **********\n"); 
  return EXIT_SUCCESS;
}
