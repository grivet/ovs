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

#ifndef STREAM_PROVIDER_H
#define STREAM_PROVIDER_H 1

#include <assert.h>
#include <sys/types.h>
#include "stream.h"

/* Active stream connection. */

/* Active stream connection.
 *
 * This structure should be treated as opaque by implementation. */
struct stream {
    struct stream_class *class;
    int state;
    int error;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint32_t local_ip;
    uint16_t local_port;
    char *name;
};

void stream_init(struct stream *, struct stream_class *, int connect_status,
                 const char *name);
void stream_set_remote_ip(struct stream *, uint32_t remote_ip);
void stream_set_remote_port(struct stream *, uint16_t remote_port);
void stream_set_local_ip(struct stream *, uint32_t local_ip);
void stream_set_local_port(struct stream *, uint16_t local_port);
static inline void stream_assert_class(const struct stream *stream,
                                       const struct stream_class *class)
{
    assert(stream->class == class);
}

struct stream_class {
    /* Prefix for connection names, e.g. "tcp", "ssl", "unix". */
    const char *name;

    /* Attempts to connect to a peer.  'name' is the full connection name
     * provided by the user, e.g. "tcp:1.2.3.4".  This name is useful for error
     * messages but must not be modified.
     *
     * 'suffix' is a copy of 'name' following the colon and may be modified.
     *
     * Returns 0 if successful, otherwise a positive errno value.  If
     * successful, stores a pointer to the new connection in '*streamp'.
     *
     * The open function must not block waiting for a connection to complete.
     * If the connection cannot be completed immediately, it should return
     * EAGAIN (not EINPROGRESS, as returned by the connect system call) and
     * continue the connection in the background. */
    int (*open)(const char *name, char *suffix, struct stream **streamp);

    /* Closes 'stream' and frees associated memory. */
    void (*close)(struct stream *stream);

    /* Tries to complete the connection on 'stream'.  If 'stream''s connection
     * is complete, returns 0 if the connection was successful or a positive
     * errno value if it failed.  If the connection is still in progress,
     * returns EAGAIN.
     *
     * The connect function must not block waiting for the connection to
     * complete; instead, it should return EAGAIN immediately. */
    int (*connect)(struct stream *stream);

    /* Tries to receive up to 'n' bytes from 'stream' into 'buffer', and
     * returns:
     *
     *     - If successful, the number of bytes received (between 1 and 'n').
     *
     *     - On error, a negative errno value.
     *
     *     - 0, if the connection has been closed in the normal fashion.
     *
     * The recv function will not be passed a zero 'n'.
     *
     * The recv function must not block waiting for data to arrive.  If no data
     * have been received, it should return -EAGAIN immediately. */
    ssize_t (*recv)(struct stream *stream, void *buffer, size_t n);

    /* Tries to send up to 'n' bytes of 'buffer' on 'stream', and returns:
     *
     *     - If successful, the number of bytes sent (between 1 and 'n').
     *
     *     - On error, a negative errno value.
     *
     *     - Never returns 0.
     *
     * The send function will not be passed a zero 'n'.
     *
     * The send function must not block.  If no bytes can be immediately
     * accepted for transmission, it should return -EAGAIN immediately. */
    ssize_t (*send)(struct stream *stream, const void *buffer, size_t n);

    /* Arranges for the poll loop to wake up when 'stream' is ready to take an
     * action of the given 'type'. */
    void (*wait)(struct stream *stream, enum stream_wait_type type);
};

/* Passive listener for incoming stream connections.
 *
 * This structure should be treated as opaque by stream implementations. */
struct pstream {
    struct pstream_class *class;
    char *name;
};

void pstream_init(struct pstream *, struct pstream_class *, const char *name);
static inline void pstream_assert_class(const struct pstream *pstream,
                                        const struct pstream_class *class)
{
    assert(pstream->class == class);
}

struct pstream_class {
    /* Prefix for connection names, e.g. "ptcp", "pssl", "punix". */
    const char *name;

    /* Attempts to start listening for stream connections.  'name' is the full
     * connection name provided by the user, e.g. "ptcp:1234".  This name is
     * useful for error messages but must not be modified.
     *
     * 'suffix' is a copy of 'name' following the colon and may be modified.
     *
     * Returns 0 if successful, otherwise a positive errno value.  If
     * successful, stores a pointer to the new connection in '*pstreamp'.
     *
     * The listen function must not block.  If the connection cannot be
     * completed immediately, it should return EAGAIN (not EINPROGRESS, as
     * returned by the connect system call) and continue the connection in the
     * background. */
    int (*listen)(const char *name, char *suffix, struct pstream **pstreamp);

    /* Closes 'pstream' and frees associated memory. */
    void (*close)(struct pstream *pstream);

    /* Tries to accept a new connection on 'pstream'.  If successful, stores
     * the new connection in '*new_streamp' and returns 0.  Otherwise, returns
     * a positive errno value.
     *
     * The accept function must not block waiting for a connection.  If no
     * connection is ready to be accepted, it should return EAGAIN. */
    int (*accept)(struct pstream *pstream, struct stream **new_streamp);

    /* Arranges for the poll loop to wake up when a connection is ready to be
     * accepted on 'pstream'. */
    void (*wait)(struct pstream *pstream);
};

/* Active and passive stream classes. */
extern struct stream_class tcp_stream_class;
extern struct pstream_class ptcp_pstream_class;
extern struct stream_class unix_stream_class;
extern struct pstream_class punix_pstream_class;

#endif /* stream-provider.h */
