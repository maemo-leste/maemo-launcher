/*
 * Copyright © 2007 Nokia Corporation
 * Copyright © 2021 Merlijn Wajer <merlijn@wizzup.org>
 *
 * Contact: Guillem Jover <guillem.jover@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <libgen.h>

#include "report.h"
#include "search.h"

static char *
merge_paths(const char *base_path, const char *rel_path)
{
  char *path;

  if (asprintf(&path, "%s%s%s", base_path,
               (base_path[strlen(base_path) - 1] == '/' ? "" : "/"),
               rel_path) < 0)
    die(1, "allocating merge path buffer");

  return path;
}


static char* path_lookup(const char *progname) {
  char *path = getenv("PATH");
  char *saveptr = NULL;
  char *token;
  char *launch = NULL;

  if (path == NULL)
    die(1, "could not get PATH environment variable");
  path = strdup(path);

  for (token = strtok_r(path, ":", &saveptr); token != NULL;
       token = strtok_r(NULL, ":", &saveptr))
  {
    launch = merge_paths(token, progname);

    if (access(launch, X_OK) == 0)
      break;

    free(launch);
    launch = NULL;
  }

  free(path);

  return launch;
}

char* resolve_program(const char *progname) {
  char *launch = NULL;
  char *tmp = NULL;
  char link[PATH_MAX];
  int count;
  int ret;

  if (progname[0] == '/')
  {
    launch = strdup(progname);
  }
  else
  {
    launch = path_lookup(progname);
  }

  if (launch) {
    tmp = launch;

    count = 0;
    while (1) {
      ret = readlink(tmp, link, PATH_MAX);

      if (ret == -1)
      {
        if (errno != EINVAL)
            die(1, "Unable to read link: %s", strerror(errno));

        return tmp;
      }
      else
      {
        char* base = strdup(link);
        char* basen = strdup(basename(base));
        free(base);
        /* The final link will be to maemo-invoker, but we don't want to
         * return that. */
        if (!strcmp(basen, "maemo-invoker")) {
            free(basen);
            return tmp;
        }

        free(basen);
        free(tmp);
        tmp = strdup(link);
        memset(link, 0, PATH_MAX);
      }
      count++;
      if (count > 999) {
        die(1, "too many levels of symbolic links");
      }
    }
  }

  return NULL;
}

char *
search_program(const char *progname)
{
  char *launch = NULL;
  char *cwd;

  if (progname[0] == '/')
  {
    launch = strdup(progname);
    if (!launch)
      die(1, "allocating program name buffer");
  }
  else if (strchr(progname, '/') != NULL)
  {
    cwd = get_current_dir_name();
    launch = merge_paths(cwd, progname);
    free(cwd);
  }
  else
  {
    launch = path_lookup(progname);

    if (launch == NULL)
      die(1, "could not locate program to launch");

    if (launch[0] != '/')
    {
      char *relative = launch;

      cwd = get_current_dir_name();
      launch = merge_paths(cwd, relative);

      free(cwd);
      free(relative);
    }
  }

  return launch;
}

