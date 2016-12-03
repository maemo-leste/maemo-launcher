/*
 * Copyright © 2008 Nokia Corporation
 *
 * Author: Guillem Jover <guillem.jover@nokia.com>
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

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "report.h"
#include "invokelib.h"
#include "comm_msg.h"

#define WORD_SIZE	sizeof(uint32_t)
#define WORD_MASK	(~(WORD_SIZE - 1))
#define WORD_ALIGN(x)	(((x) + WORD_SIZE - 1) & WORD_MASK)

struct comm_msg {
  size_t size;
  size_t size_max;
  size_t used;
  size_t read;
  void *buf;
};

comm_msg_t *
comm_msg_new(size_t size, size_t size_max)
{
  comm_msg_t *msg;

  if (size_max)
    assert(size <= size_max);

  msg = malloc(sizeof(*msg));
  if (!msg)
  {
    error("allocating comm message\n");
    return NULL;
  }

  msg->used = msg->read = 0;
  msg->size = WORD_ALIGN(size);
  msg->size_max = WORD_ALIGN(size_max);
  msg->buf = malloc(msg->size);
  if (!msg->buf)
  {
    free(msg);
    error("allocating comm message buffer\n");
    return NULL;
  }

  return msg;
}

bool
comm_msg_destroy(comm_msg_t *msg)
{
  if (msg->buf)
  {
    msg->size = msg->size_max = msg->used = msg->read = 0;
    free(msg->buf);
    msg->buf = NULL;
  }

  return true;
}

bool
comm_msg_print(comm_msg_t *msg, const char *func)
{
  uint32_t i, *p = msg->buf;

  info("%s: size: %08x\n", func, msg->size);

  for (i = 0; i < msg->used / sizeof(msg->used); i++)
    info("%s: data[%04x]: %08x\n", func, i, p[i]);

  return true;
}

bool
comm_msg_grow(comm_msg_t *msg, size_t need_size)
{
  size_t end_size;
  void *p;

  if (msg->size - msg->used >= need_size)
    return true;

  end_size = msg->size + need_size;

  if (msg->size_max)
  {
    if (end_size > msg->size_max)
      return false;

    if (end_size + msg->size <= msg->size_max)
      end_size += msg->size;
  }
  else
    end_size += msg->size;

  p = realloc(msg->buf, end_size);
  if (!p)
    return false;

  msg->buf = p;
  memset((uint8_t *)(msg->buf) + msg->used, 0, end_size - msg->used);

  msg->size = end_size;

  return true;
}

bool
comm_msg_reset(comm_msg_t *msg)
{
  msg->used = msg->read = 0;

  return true;
}

/*
 * Low level put/get functions.
 */

static void
comm_msg_put_u32(comm_msg_t *msg, uint32_t u)
{
  memcpy((uint8_t *)(msg->buf) + msg->used, &u, sizeof(u));
  msg->used += sizeof(u);
}

static void
comm_msg_get_u32(comm_msg_t *msg, uint32_t *u)
{
  memcpy(u, (uint8_t *)(msg->buf) + msg->read, sizeof(*u));
  msg->read += sizeof(*u);
}

static void
comm_msg_put_size(comm_msg_t *msg, size_t s)
{
  memcpy((uint8_t *)(msg->buf) + msg->used, &s, sizeof(s));
  msg->used += sizeof(s);
}

static void
comm_msg_get_size(comm_msg_t *msg, size_t *s)
{
  memcpy(s, (uint8_t *)(msg->buf) + msg->read, sizeof(*s));
  msg->read += sizeof(*s);
}

/*
 * High level put/get functions.
 */

bool
comm_msg_put_magic(comm_msg_t *msg, uint32_t magic)
{
  if (!comm_msg_grow(msg, sizeof(magic)))
    return false;

  comm_msg_put_u32(msg, magic);

  return true;
}

bool
comm_msg_get_magic(comm_msg_t *msg, uint32_t *magic)
{
  if (msg->read + sizeof(*magic) > msg->used)
    return false;

  comm_msg_get_u32(msg, magic);

  return true;
}

bool
comm_msg_put_int(comm_msg_t *msg, int32_t i)
{
  static const size_t size = sizeof(i);

  if (!comm_msg_grow(msg, size + sizeof(size)))
    return false;

  /* Pack the size. */
  comm_msg_put_size(msg, size);

  /* Pack the data. */
  comm_msg_put_u32(msg, i);

  return true;
}

bool
comm_msg_get_int(comm_msg_t *msg, int32_t *i)
{
  size_t size;
  uint32_t u;

  if (msg->read + sizeof(size) + sizeof(*i) > msg->used)
    return false;

  comm_msg_get_size(msg, &size);

  if (size != sizeof(*i))
    return false;

  comm_msg_get_u32(msg, &u);
  *i = u;

  return true;
}

bool
comm_msg_put_str(comm_msg_t *msg, const char *str)
{
  size_t size = strlen(str) + 1;
  size_t aligned_size = WORD_ALIGN(size);
  size_t pad_size = aligned_size - size;

  if (!comm_msg_grow(msg, aligned_size + sizeof(size)))
    return false;

  /* Pack the size. */
  comm_msg_put_size(msg, aligned_size);

  /* Pack the data. */
  memcpy((uint8_t *)(msg->buf) + msg->used, str, size);
  msg->used += size;

  if (pad_size)
  {
    /* Add padding, if needed. */
    memset((uint8_t *)(msg->buf) + msg->used, 0, pad_size);
    msg->used += pad_size;
  }

  return true;
}

bool
comm_msg_get_str(comm_msg_t *msg, const char **str_r)
{
  size_t size;
  const char *str;

  if (msg->read + sizeof(size) > msg->used)
    return false;

  comm_msg_get_size(msg, &size);

  /* Keep a pointer to the data. */
  str = (char *)(msg->buf) + msg->read;
  msg->read += size;

  if (msg->read > msg->used)
    return false;

  if ((size - strlen(str)) > WORD_SIZE)
    return false;

  *str_r = str;

  return true;
}

bool
comm_msg_send(int fd, comm_msg_t *msg)
{
  const bool result =
       (sizeof(msg->used) == write(fd, &msg->used, sizeof(msg->used))) &&
       (msg->used == write(fd, msg->buf, msg->used));

#if DEBUG
  comm_msg_print(msg, __FUNCTION__);
#endif

  return result;
}

bool
comm_msg_recv(int fd, comm_msg_t *msg)
{
  size_t size;

  if ( invoke_raw_read(fd, &size, sizeof(size)) )
    return false;

  if (!comm_msg_grow(msg, size))
    return false;

  msg->used = size;
  if ( invoke_raw_read(fd, msg->buf, size) )
    return false;

#if DEBUG
  comm_msg_print(msg, __FUNCTION__);
#endif

  return true;
}

