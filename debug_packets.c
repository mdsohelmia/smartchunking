#include <libavformat/avformat.h>
#include <stdio.h>

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    fprintf(stderr, "Usage: %s <input.mp4>\n", argv[0]);
    return 1;
  }

  AVFormatContext *fmt = NULL;
  if (avformat_open_input(&fmt, argv[1], NULL, NULL) < 0)
  {
    fprintf(stderr, "Failed to open input\n");
    return 1;
  }

  if (avformat_find_stream_info(fmt, NULL) < 0)
  {
    fprintf(stderr, "Failed to find stream info\n");
    avformat_close_input(&fmt);
    return 1;
  }

  AVPacket *pkt = av_packet_alloc();
  int count = 0;

  while (av_read_frame(fmt, pkt) >= 0 && count < 20)
  {
    printf("stream=%d pts=%lld dts=%lld dur=%lld size=%d flags=%s\n",
           pkt->stream_index,
           pkt->pts,
           pkt->dts,
           pkt->duration,
           pkt->size,
           (pkt->flags & AV_PKT_FLAG_KEY) ? "K" : "_");
    av_packet_unref(pkt);
    count++;
  }

  av_packet_free(&pkt);
  avformat_close_input(&fmt);
  return 0;
}
