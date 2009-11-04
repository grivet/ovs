/*
 * Copyright (c) 2009 Nicira Networks.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef STREAM_H
#define STREAM_H 1

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flow.h"

struct pstream;
struct stream;

void stream_usage(const char *name, bool active, bool passive);

/* Bidirectional byte streams. */
int stream_open(const char *name, struct stream **);
int stream_open_block(const char *name, struct stream **);
void stream_close(struct stream *);
const char *stream_get_name(const struct stream *);
uint32_t stream_get_remote_ip(const struct stream *);
uint16_t stream_get_remote_port(const struct stream *);
uint32_t stream_get_local_ip(const struct stream *);
uint16_t stream_get_local_port(const struct stream *);
int stream_connect(struct stream *);
int stream_recv(struct stream *, void *buffer, size_t n);
int stream_send(struct stream *, const void *buffer, size_t n);

enum stream_wait_type {
    STREAM_CONNECT,
    STREAM_RECV,
    STREAM_SEND
};
void stream_wait(struct stream *, enum stream_wait_type);
void stream_connect_wait(struct stream *);
void stream_recv_wait(struct stream *);
void stream_send_wait(struct stream *);

/* Passive streams: listeners for incoming stream connections. */
int pstream_open(const char *name, struct pstream **);
const char *pstream_get_name(const struct pstream *);
void pstream_close(struct pstream *);
int pstream_accept(struct pstream *, struct stream **);
void pstream_wait(struct pstream *);

#endif /* stream.h */
