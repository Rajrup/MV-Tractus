#include <iostream>
#include <string>
#include <vector>

// FFmpeg headers
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/motion_vector.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// OpenCV headers
#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>

static void drawMotionVectors(cv::Mat &frame, const AVFrame *avFrame)
{
    std::cout << "drawMotionVectors: frame size: " << frame.size() << std::endl;
    if (!avFrame) return;

    // Obtain side data containing motion vectors
    AVFrameSideData *sd = av_frame_get_side_data(avFrame, AV_FRAME_DATA_MOTION_VECTORS);
    if (!sd) {
        std::cerr << "No motion vector side data found." << std::endl;
        return; // No motion vector side data found
    }

    // Each motion vector is stored in an AVMotionVector struct
    const AVMotionVector *mvs = (const AVMotionVector *)sd->data;
    int mv_count = sd->size / sizeof(AVMotionVector);

    std::cout << "Found " << mv_count << " motion vectors." << std::endl;
    for (int i = 0; i < mv_count; i++) {
        const AVMotionVector &mv = mvs[i];

        // We only care about forward predicted blocks (for P-frames)
        if(avFrame->pict_type != AV_PICTURE_TYPE_P)
        {
            std::cout << "Frame type: " << av_get_picture_type_char(avFrame->pict_type) << ". Skipping." << std::endl;
            continue;
        }

        // mv.w and mv.h are the block dimensions.
        // src_x, src_y is top-left of the block, in 1/4 pixel coordinates in some codecs.
        // dst_x, dst_y is the predicted block location, also in 1/4 pixel coordinates.
        // motion_x, motion_y is the actual motion vector in 1/4 pixel increments.

        // Convert from 1/4-pixel to full-pixel coordinates
        int src_x = mv.src_x / 4;
        int src_y = mv.src_y / 4;
        int dst_x = src_x + mv.motion_x / 4;
        int dst_y = src_y + mv.motion_y / 4;

        // Make sure we’re within frame bounds
        if (src_x < 0 || src_y < 0 || src_x >= frame.cols || src_y >= frame.rows) 
            continue;
        if (dst_x < 0 || dst_y < 0 || dst_x >= frame.cols || dst_y >= frame.rows) 
            continue;

        // Draw the motion vector
        cv::Point2i p1(src_x, src_y);
        cv::Point2i p2(dst_x, dst_y);
        cv::arrowedLine(frame, p1, p2, cv::Scalar(0, 0, 255), 1, cv::LINE_AA);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_video>\n";
        return -1;
    }

    std::string inputFile = argv[1];

    std::cout << "Input file: " << inputFile << std::endl;

    // Register all formats and codecs (depending on FFmpeg version, might be optional)
    av_register_all();

    // Open input file
    AVFormatContext *fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, inputFile.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Could not open input file: " << inputFile << std::endl;
        return -1;
    }
    std::cout << "Opened input file." << std::endl;

    // Retrieve stream information
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream information." << std::endl;
        avformat_close_input(&fmt_ctx);
        return -1;
    }
    std::cout << "Found stream information." << std::endl;

    // Find the video stream index
    int video_stream_index = -1;
    AVCodecParameters *codecpar = nullptr;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            codecpar = fmt_ctx->streams[i]->codecpar;
            break;
        }
    }

    if (video_stream_index < 0) {
        std::cerr << "Could not find a video stream in the file." << std::endl;
        avformat_close_input(&fmt_ctx);
        return -1;
    }
    std::cout << "Found video stream at index: " << video_stream_index << std::endl;

    // Find decoder for the video stream
    AVCodec *decoder = avcodec_find_decoder(codecpar->codec_id);
    if (!decoder) {
        std::cerr << "Unsupported codec!" << std::endl;
        avformat_close_input(&fmt_ctx);
        return -1;
    }
    std::cout << "Found decoder for the codec." << std::endl;

    // Allocate a codec context for the decoder
    AVCodecContext *codec_ctx = avcodec_alloc_context3(decoder);
    if (!codec_ctx) {
        std::cerr << "Could not allocate AVCodecContext." << std::endl;
        avformat_close_input(&fmt_ctx);
        return -1;
    }
    std::cout << "Allocated codec context." << std::endl;

    // Copy codec parameters from input stream to codec context
    if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
        std::cerr << "Failed to copy codec parameters to decoder context." << std::endl;
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }
    std::cout << "Copied codec parameters to codec context." << std::endl;

    // Open the codec
    // Create a dictionary for decoder options
    AVDictionary* codec_opts = nullptr;
    av_dict_set(&codec_opts, "flags2", "+export_mvs", 0);

    if (avcodec_open2(codec_ctx, decoder, &codec_opts) < 0) {
        std::cerr << "Could not open codec." << std::endl;
        av_dict_free(&codec_opts);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }
    std::cout << "Opened codec." << std::endl;
    av_dict_free(&codec_opts);

    // Prepare to read frames
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame  = av_frame_alloc();
    if (!packet || !frame) {
        std::cerr << "Could not allocate packet or frame." << std::endl;
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }
    std::cout << "Allocated packet and frame." << std::endl;

    // Prepare SWS context for image conversion to BGR (for OpenCV)
    struct SwsContext *sws_ctx = nullptr;

    // Reading packet loop
    while (av_read_frame(fmt_ctx, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            // Send the packet to the decoder
            if (avcodec_send_packet(codec_ctx, packet) == 0) {
                // Receive decoded frames
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    // Print frame index
                    std::cout << "Frame: " << codec_ctx->frame_number << std::endl;

                    // Print type of frame (I, P, B, etc.)
                    std::cout << "Frame type: ";
                    switch (frame->pict_type) {
                        case AV_PICTURE_TYPE_I: std::cout << "I"; break;
                        case AV_PICTURE_TYPE_P: std::cout << "P"; break;
                        case AV_PICTURE_TYPE_B: std::cout << "B"; break;
                        default: std::cout << "Other"; break;
                    }
                    std::cout << std::endl;

                    // Initialize sws context if not done yet
                    if (!sws_ctx) {
                        sws_ctx = sws_getContext(codec_ctx->width, codec_ctx->height,
                                                 codec_ctx->pix_fmt,
                                                 codec_ctx->width, codec_ctx->height,
                                                 AV_PIX_FMT_BGR24,
                                                 SWS_BICUBIC, nullptr, nullptr, nullptr);
                    }

                    // Convert the frame to BGR (OpenCV format)
                    cv::Mat bgrImage(codec_ctx->height, codec_ctx->width, CV_8UC3);
                    uint8_t *dst_data[4] = { bgrImage.data, nullptr, nullptr, nullptr };
                    int dst_linesize[4]  = { static_cast<int>(bgrImage.step), 0, 0, 0 };

                    sws_scale(sws_ctx,
                              frame->data, frame->linesize,
                              0, codec_ctx->height,
                              dst_data, dst_linesize);

                    // Draw motion vectors on top of the bgrImage
                    drawMotionVectors(bgrImage, frame);

                    // Show the frame in an OpenCV window
                    cv::imshow("Motion Vectors", bgrImage);
                    // Press ESC or 'q' to exit
                    char c = (char)cv::waitKey(1);
                    if (c == 27 || c == 'q') {
                        av_packet_unref(packet);
                        goto end;
                    }
                }
            }
        }
        av_packet_unref(packet);
    }

    // Flush the decoder
    avcodec_send_packet(codec_ctx, nullptr);
    while (avcodec_receive_frame(codec_ctx, frame) == 0) {
        // Process remaining frames if needed...
        // (Same as above: sws_scale, drawMotionVectors, imshow, etc.)
    }

end:
    // Cleanup
    cv::destroyAllWindows();
    sws_freeContext(sws_ctx);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    return 0;
}
