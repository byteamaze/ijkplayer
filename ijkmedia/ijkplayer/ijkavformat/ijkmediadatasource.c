/*
 * Copyright (c) 2015 Bilibili
 * Copyright (c) 2015 Zhang Rui <bbcallen@gmail.com>
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <assert.h>
#include "libavformat/avformat.h"
#include "libavformat/url.h"
#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"

#include "ijkioapplication.h"
#include "ijkplayer/ijkavutil/opt.h"

typedef struct Context {
    AVClass        *class;

    /* options */
    int64_t         logical_pos;
    int64_t         logical_size;

    int64_t         media_data_source_ptr;
    IJKDataSourceContext   *media_data_source;
    int             jbuffer_capacity;
} Context;

// 打开数据
static int ijkmds_open(URLContext *h, const char *arg, int flags, AVDictionary **options)
{
    Context *c = h->priv_data;
    char *final = NULL;

    // 截取内存地址
    av_strstart(arg, "ijkmediadatasource:", &arg);

    // 内存地址转换为指针
    intptr_t media_data_source_ptr = (intptr_t) strtoll(arg, &final, 10);
    IJKDataSourceContext *media_data_source = ((IJKDataSourceContext *)media_data_source_ptr);
    if (!media_data_source)
        return AVERROR(EINVAL);

    // 获取内容大小
    c->logical_size = media_data_source->total_size(media_data_source->data_source);
    if (c->logical_size < 0) {
        h->is_streamed = 1;
        c->logical_size = -1;
    }

    c->media_data_source = media_data_source;
    c->media_data_source_ptr = media_data_source_ptr;
    if (!c->media_data_source) {
        return AVERROR(ENOMEM);
    }

    return 0;
}

// 关闭数据源
static int ijkmds_close(URLContext *h)
{
    Context *c = h->priv_data;
    IJKDataSourceContext *mds = c->media_data_source;
    
    if (mds) { // 关闭文件流
        mds->close(mds->data_source);
    }
    c->media_data_source_ptr = 0;

    return 0;
}

//static jobject jbuffer_grow(JNIEnv *env, URLContext *h, int new_capacity) {
//    Context *c = h->priv_data;
//
//    if (c->jbuffer && c->jbuffer_capacity >= new_capacity)
//        return c->jbuffer;
//
//    new_capacity = FFMAX(new_capacity, c->jbuffer_capacity * 2);
//
//    J4A_DeleteGlobalRef__p(env, &c->jbuffer);
//    c->jbuffer_capacity = 0;
//
//    c->jbuffer = J4A_NewByteArray__asGlobalRef__catchAll(env, new_capacity);
//    if (J4A_ExceptionCheck__catchAll(env) || !c->jbuffer) {
//        c->jbuffer = NULL;
//        return NULL;
//    }
//
//    c->jbuffer_capacity = new_capacity;
//    return c->jbuffer;
//}

// 读取数据
static int ijkmds_read(URLContext *h, unsigned char *buf, int size)
{
    Context    *c = h->priv_data;
    IJKDataSourceContext *mds = c->media_data_source;
    int        ret = 0;

    if (!mds)
        return AVERROR(EINVAL);

    ret = mds->read_packet(mds->data_source, buf, c->logical_pos, size);

    if (ret < 0)
        return AVERROR_EOF;
    else if (ret == 0)
        return AVERROR(EAGAIN);

    c->logical_pos += ret;
    return ret;
}

// 移动到指定数据位置
static int64_t ijkmds_seek(URLContext *h, int64_t pos, int whence)
{
    Context *c = h->priv_data;
    IJKDataSourceContext *mds = c->media_data_source;
//    int64_t  ret;
    int64_t  new_logical_pos;

    if (!mds)
        return AVERROR(EINVAL);

    if (whence == AVSEEK_SIZE) {
        av_log(h, AV_LOG_TRACE, "%s: AVSEEK_SIZE: %"PRId64"\n", __func__, (int64_t)c->logical_size);
        return c->logical_size;
    } else if (whence == SEEK_CUR) {
        av_log(h, AV_LOG_TRACE, "%s: %"PRId64"\n", __func__, pos);
        new_logical_pos = pos + c->logical_pos;
    } else if (whence == SEEK_SET){
        av_log(h, AV_LOG_TRACE, "%s: %"PRId64"\n", __func__, pos);
        new_logical_pos = pos;
    } else {
        return AVERROR(EINVAL);
    }
    if (new_logical_pos < 0)
        return AVERROR(EINVAL);

//    ret = c->media_data_source->read_packet(c->media_data_source->data_source, new_logical_pos, jbuffer, 0, 0);
//    ret = mds->seek_packet(mds->data_source, pos, whence);
//    if (ret < 0)
//        return AVERROR_EOF;

    c->logical_pos = new_logical_pos;
    return c->logical_pos;
}

#define OFFSET(x) offsetof(Context, x)
#define D AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { NULL }
};

#undef D
#undef OFFSET

static const AVClass ijkmediadatasource_context_class = {
    .class_name = "IjkMediaDataSource",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

URLProtocol ijkimp_ff_ijkmediadatasource_protocol = {
    .name                = "ijkmediadatasource",
    .url_open2           = ijkmds_open,
    .url_read            = ijkmds_read,
    .url_seek            = ijkmds_seek,
    .url_close           = ijkmds_close,
    .priv_data_size      = sizeof(Context),
    .priv_data_class     = &ijkmediadatasource_context_class,
};
