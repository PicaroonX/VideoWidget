﻿extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "libavutil/avassert.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
}
#include <QDateTime>
#include <QDebug>
#include "../renderthread.h"
#include "ffmpegcpudecode.h"

FFmpegCpuDecode::FFmpegCpuDecode(DecodeTaskManagerImpl *taskManger, RenderThread *render_thr):
    DecodeTask(render_thr),
    taskManager_(taskManger)
{

}

FFmpegCpuDecode::~FFmpegCpuDecode()
{
    if(buffer_)
    {
        av_free(buffer_);
        buffer_ = nullptr;
    }
    qDebug() << "FFmpegCpuDecode::~FFmpegCpuDecode()";
}

void FFmpegCpuDecode::decode(const QString &url)
{
    AVFormatContext *pFormatCtx = nullptr;
    AVCodecContext *pCodecCtx = nullptr;
    AVCodec *pCodec = nullptr;
    AVFrame *pFrame = nullptr;
    uint8_t *out_buffer;
    AVPacket packet;
    AVStream *video = nullptr;
    char errorbuf[1024]{0};
    QString errorMsg;
    int videoStream, i;
    int ret;
    int vden;
    int vnum;

    AVDictionary *opt = nullptr;
    //av_dict_set(&opt,"buffer_size","1024000",0);
    //av_dict_set(&opt,"max_delay","0",0);
    av_dict_set(&opt,"rtsp_transport","tcp",0);
    av_dict_set(&opt,"stimeout","5000000",0);
    if ((ret = avformat_open_input(&pFormatCtx, url.toUtf8().data(), nullptr, &opt)) != 0) {
        av_strerror(ret, errorbuf, sizeof(errorbuf));
        errorMsg = errorbuf;
        thread()->sigError(errorMsg);
        goto  END;
    }

    if ((ret = avformat_find_stream_info(pFormatCtx, nullptr)) < 0) {
        av_strerror(ret, errorbuf, sizeof(errorbuf));
        errorMsg = errorbuf;
        thread()->sigError(errorMsg);
        goto  END;
    }

    if((ret = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0)) < 0){
        av_strerror(ret, errorbuf, sizeof(errorbuf));
        errorMsg = errorbuf;
        thread()->sigError(errorMsg);
        goto  END;
    }
    videoStream = ret;
    video = pFormatCtx->streams[videoStream];

    if(!(pCodec = avcodec_find_decoder(video->codecpar->codec_id)))
    {
        av_strerror(ret, errorbuf, sizeof(errorbuf));
        errorMsg = QString("avcodec_find_decoder error: %1").arg(errorbuf);
        thread()->sigError(errorMsg);
        goto  END;
    }

    if (!(pCodecCtx = avcodec_alloc_context3(pCodec)))
    {
        errorMsg = QString("avcodec_alloc_context3(pCodec) error: %1").arg(AVERROR(ENOMEM));
        thread()->sigError(errorMsg);
        goto  END;
    }

    if ((ret = avcodec_parameters_to_context(pCodecCtx, video->codecpar)) < 0)
    {
        av_strerror(ret, errorbuf, sizeof(errorbuf));
        errorMsg = errorbuf;
        thread()->sigError(errorMsg);
        goto  END;
    }

    vden = video->avg_frame_rate.den;
	vnum = video->avg_frame_rate.num;
    if(vden <= 0)
    {
        errorMsg = "get fps failed";
        thread()->sigError(errorMsg);
        goto  END;
    }
    thread()->sigFps(vnum/vden);
    stream_time_base_ = video->time_base;

    ///打开解码器
    if ((ret = avcodec_open2(pCodecCtx, pCodec, nullptr)) < 0) {
        av_strerror(ret, errorbuf, sizeof(errorbuf));
        errorMsg = errorbuf;
        thread()->sigError(errorMsg);
        goto  END;
    }

    pFrame = av_frame_alloc();
    av_dump_format(pFormatCtx, 0, url.toUtf8().data(), 0); //输出视频信息

    while (!thread()->isInterruptionRequested() && ret >= 0)
    {
        if ((ret = av_read_frame(pFormatCtx, &packet)) < 0)
        {
            if(ret != AVERROR_EOF)
            {
                av_strerror(ret, errorbuf, sizeof(errorbuf));
                errorMsg = errorbuf;
                thread()->sigError(errorMsg);
            }
            break; //这里认为视频读取完了
        }

        if (packet.stream_index == videoStream) {
            ret = decode_packet(pCodecCtx, &packet, pFrame);
        }

        qint64 end_pt = QDateTime::currentMSecsSinceEpoch();
        if((end_pt - start_pt_) >= 1000)
        {
            if(decode_frames_ != curFps_)
            {
                curFps_ = decode_frames_;
                thread()->sigCurFpsChanged(curFps_);
            }
            start_pt_ = end_pt;
            decode_frames_ = 0;
        }
        decode_frames_++;

        av_packet_unref(&packet);
    }
    packet.data = nullptr;
    packet.size = 0;
//    ret = decode_packet(pCodecCtx, &packet, pFrame);

END:
    if(pFrame)
    {
        av_frame_free(&pFrame);
    }
    if(pCodecCtx)
    {
        avcodec_free_context(&pCodecCtx);
    }
    if(pFormatCtx)
    {
        avformat_close_input(&pFormatCtx);
    }
    thread()->sigCurFpsChanged(0);
}

void FFmpegCpuDecode::destroy()
{
    if(render_){
        delete render_;
        render_ = nullptr;
    }
}

int FFmpegCpuDecode::decode_packet(AVCodecContext *pCodecCtx, AVPacket *packet, AVFrame *pFrame)
{
    int ret;
    QString errorMsg;
    char errorbuf[1024]{0};

    if((ret = avcodec_send_packet(pCodecCtx, packet)) < 0 ){
        av_strerror(ret, errorbuf, sizeof(errorbuf));
        errorMsg = QString("Error during avcodec_send_packet: %1").arg(errorbuf);
        thread()->sigError(errorMsg);
        return ret;
    }

    while (true) {
        ret = avcodec_receive_frame(pCodecCtx, pFrame);
        if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            break;
        }else if(ret < 0){
            av_strerror(ret, errorbuf, sizeof(errorbuf));
            errorMsg = QString("Error while avcodec_receive_frame: %1").arg(errorbuf);
            thread()->sigError(errorMsg);
            goto fail;
        }

#if 0
        thread()->Render([&](){
            if(!render_)
            {
                render_ = thread()->getRender(pFrame->format);
                render_->initialize(pFrame->width, pFrame->height);
            }
            render_->render(pFrame->data, pFrame->linesize, pFrame->width, pFrame->height);
        });
#else

//        pFrame->pts =  pFrame->best_effort_timestamp;
//        if(pFrame->pts != AV_NOPTS_VALUE)
//        {
//            if(last_pts_ != AV_NOPTS_VALUE)
//            {
//                AVRational ra;
//                ra.num = 1;
//                ra.den = AV_TIME_BASE;
//                int64_t delay = av_rescale_q(pFrame->pts - last_pts_, stream_time_base_, ra);
//                if(delay > 0 && delay < 1000000)
//                {
//                    QThread::usleep(delay);
//                }
//            }
//            last_pts_ = pFrame->pts;
//        }
        if(!buffer_)
        {
            bufferSize_ = av_image_get_buffer_size(AVPixelFormat(pFrame->format), pFrame->width,
                                                  pFrame->height, 1);
            buffer_ = (uint8_t*) av_malloc(bufferSize_);
        }

        ret = av_image_copy_to_buffer(buffer_, bufferSize_,
                    (const uint8_t * const *)pFrame->data,
                    (const int *)pFrame->linesize, AVPixelFormat(pFrame->format),
                    pFrame->width, pFrame->height, 1);

        thread()->Render([&](){
            if(!render_)
            {
                render_ = thread()->getRender(pFrame->format);
                render_->initialize(pFrame->width, pFrame->height);
            }
            render_->render(buffer_, pFrame->width, pFrame->height);
        });
#endif

    fail:
        if(ret < 0){
            return  ret;
        }
    }

    return 0;
}
