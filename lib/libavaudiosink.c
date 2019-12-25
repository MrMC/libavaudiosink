#import <atomic>
#import <string>
#import <stdlib.h>
#import <unistd.h>
#import <sys/stat.h>

#import <AVFoundation/AVFoundation.h>
#import <AVFoundation/AVAudioSession.h>

#pragma mark - AudioResourceLoader
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
@interface AudioResourceLoader : NSObject <AVAssetResourceLoaderDelegate>
@property (atomic) float currentTime;
@property (atomic) float bufferedTime;
@property (atomic) float minBufferedTime;
- (id)initWithFrameBytes:(unsigned int)frameBytes;
- (void)abort;
@end

@implementation AudioResourceLoader
  std::atomic<bool>  mAbortflag;
  char *mReadbuffer;
  size_t mReadbufferSize;
  int mFileReader;
  float mFrameBytes;
  float mFrameDuration;
  int64_t mBufferedBytes;

- (id)initWithFrameBytes:(unsigned int)frameBytes
{
  self = [super init];
  if (self)
  {
    _currentTime = 0.0f;
    _bufferedTime = 0.0f;
    _minBufferedTime = 2.5f;
    mAbortflag = false;
    mFrameBytes = frameBytes;
    mFrameDuration = (256.0 * 6) / 48000.0; // 0.032 seconds
    mBufferedBytes = 0;
    // readbufferSize must be greater that 65536.
    mReadbufferSize = mFrameBytes * 40;
    if (mReadbufferSize < 65536)
      mReadbufferSize = 65536;
    mReadbuffer = new char[mReadbufferSize];
    std::string tmpBufferFile = [NSTemporaryDirectory() UTF8String];
    tmpBufferFile += "avaudio.ec3";
    mFileReader = open(tmpBufferFile.c_str(), O_RDONLY, 00666);

  }
  return self;
}

- (void)dealloc
{
  close(mFileReader);
}

- (void)abort
{
  mAbortflag = true;
}

- (bool)checkFileBufferLength:(size_t)offset length:(size_t)length
{
  struct stat statbuf;
  fstat(mFileReader, &statbuf);
  off_t filelength = statbuf.st_size;
  //NSLog(@"resourceLoader: file size %lld", filelength);
  if (filelength >= offset + length)
    return true;

  return false;
}

- (NSError *)loaderCancelledError
{
  NSError *error = [[NSError alloc] initWithDomain:@"AudioResourceLoaderErrorDomain"
    code:-1 userInfo:@{NSLocalizedDescriptionKey:@"avloader cancelled"}];
  mAbortflag = true;
  return error;
}

#define logDataRequest 0
- (BOOL)resourceLoader:(AVAssetResourceLoader *)resourceLoader shouldWaitForLoadingOfRequestedResource:(AVAssetResourceLoadingRequest *)loadingRequest
{
  AVAssetResourceLoadingContentInformationRequest* contentRequest = loadingRequest.contentInformationRequest;

  if (mAbortflag)
  {
    [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
    return YES;
  }

  if (contentRequest)
  {
    // only handles eac3
    contentRequest.contentType = @"public.enhanced-ac3-audio";
    // (2147483647) or 383479.222678571428571 seconds at 0.032 secs per 1792 byte frame
    // or 10.652200629960317 hours.
    // atmos is 2304 byte frames.
    contentRequest.contentLength = INT_MAX;
    // must be 'NO' to get player to start playing immediately
    contentRequest.byteRangeAccessSupported = NO;
    //NSLog(@"resourceLoader contentRequest %@", contentRequest);
  }

  AVAssetResourceLoadingDataRequest* dataRequest = loadingRequest.dataRequest;
  if (dataRequest)
  {
    //NSLog(@"resourceLoader dataRequest %@", dataRequest);

    // overflow throttle, keep about 3-4 seconds buffered in avplayer
    _bufferedTime = mFrameDuration * ((float)mBufferedBytes / mFrameBytes);
    while (!mAbortflag && _bufferedTime - _currentTime > _minBufferedTime)
    {
      usleep(5 * 1000);
      //NSLog(@"resourceLoader bufferedTime %f, currentTime %f", _bufferedTime, _currentTime);
    }

    size_t offset = dataRequest.requestedOffset;
    if (dataRequest.requestedLength == 2)
    {
      // AVAssetResourceLoader always 1st reads two bytes to check for a content tag.
      // ac3/eac3 has two byte tag of 0x0b77, \v is vertical tab == 0x0b
      [dataRequest respondWithData:[NSData dataWithBytes:"\vw" length:2]];
#if logDataRequest
      NSLog(@"resourceLoader: probing 1, sending 2 bytes");
#endif
      [loadingRequest finishLoading];
    }
    else if (offset != mBufferedBytes)
    {
      // more probing, grr feed the pig the ac3/eac3 two byte tag again
      // AVAssetResourceLoader needs to get something 'valid'.
      [dataRequest respondWithData:[NSData dataWithBytes:"\vw" length:2]];
#if logDataRequest
      NSLog(@"resourceLoader: probing 1, currentOffset %lu", (unsigned long)dataRequest.currentOffset);
#endif
      [loadingRequest finishLoading];
    }
    else
    {
      size_t length = mReadbufferSize;
      if (dataRequest.requestedLength == 65536)
      {
        // 1st 64k read attempt or probes
        offset = 0;
        length = 65536;
      }

      // Pull audio from buffer
      while (!mAbortflag && ![self checkFileBufferLength:offset length:length])
      {
        usleep(5 * 1000);
      }
      lseek(mFileReader, offset, SEEK_SET);
      size_t availableBytes = read(mFileReader, mReadbuffer, length);

      // check if we have enough data
      if (availableBytes > 0)
      {
        size_t requestedLength = (size_t)dataRequest.requestedLength;
        size_t bytesToCopy = requestedLength > availableBytes ? availableBytes : requestedLength;
        if (bytesToCopy > 0)
        {
            NSData *data = [NSData dataWithBytes:mReadbuffer length:bytesToCopy];
            [dataRequest respondWithData:data];
#if logDataRequest
            NSLog(@"resourceLoader: probing 0, sending %lu bytes, ending offset %llu",
              (unsigned long)[data length], dataRequest.currentOffset);
#endif
            mBufferedBytes = dataRequest.currentOffset;
        }
        [loadingRequest finishLoading];
      }
      else
      {
        // maybe return an empty buffer so silence is played until we have data
#if logDataRequest
        NSLog(@"resourceLoader: loading finished");
#endif
        [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
      }
    }
  }

  return YES;
}

- (void)resourceLoader:(AVAssetResourceLoader *)resourceLoader
  didCancelLoadingRequest:(AVAssetResourceLoadingRequest *)loadingRequest
{
  #if logDataRequest
    NSLog(@"resourceLoader: didCancelLoadingRequest");
  #endif
  mAbortflag = true;
}

@end

#pragma mark - AVPlayerSink
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
@interface AVPlayerSink : NSObject
- (id)initWithFrameSize:(unsigned int)frameSize;
- (void)close;
- (int)addPackets:(uint8_t*)data size:(unsigned int)size;
- (void)play:(bool) state;
- (void)start;
- (bool)loaded;
- (void)flush;
- (double)currentTime;
- (double)bufferedTime;
@end

@interface AVPlayerSink ()
@property (nonatomic) bool loadedFlag;
@property (nonatomic) dispatch_queue_t serialQueue;
@property (nonatomic) AVPlayer *avplayer;
@property (nonatomic) AVPlayerItem *playerItem;
@property (nonatomic) unsigned int frameSize;
@property (nonatomic) NSString *contentType;
@property (nonatomic) AudioResourceLoader *avLoader;
@end

@implementation AVPlayerSink
  int mFileWriter;
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
- (id)initWithFrameSize:(unsigned int)frameSize;
{
  self = [super init];
  if (self)
  {
    _loadedFlag = false;
    _frameSize = frameSize;
    _serialQueue = dispatch_queue_create("com.mrmc.loaderqueue", DISPATCH_QUEUE_SERIAL);
    mFileWriter = 0;
    _avplayer = [[AVPlayer alloc] init];
  }
  NSString *name = [NSProcessInfo processInfo].processName;
  if (![name hasPrefix:[NSString stringWithUTF8String:"MrMC"]])
  {
    //NSLog(@"avloader processName =  %@", name);
    return nil;
  }

  return self;
}

//-----------------------------------------------------------------------------------
- (void)close
{
  close(mFileWriter), mFileWriter = 0;

  [_avplayer.currentItem.asset cancelLoading];
  [_avplayer replaceCurrentItemWithPlayerItem:nil];
  _avplayer = nullptr;
  [_playerItem removeObserver:self forKeyPath:@"status"];
  [_playerItem.asset cancelLoading];
  _playerItem = nullptr;
  [_avLoader abort];
  _avLoader = nullptr;
}

//-----------------------------------------------------------------------------------
- (void)dealloc
{
  [self close];
}

//-----------------------------------------------------------------------------------
- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context
{
  if (object == _playerItem && [keyPath isEqualToString:@"status"])
  {
      if (_avplayer.currentItem.status == AVPlayerItemStatusReadyToPlay)
      {
        _loadedFlag = true;
#if logDataRequest
        NSLog(@"resourceLoader: loaded true");
#endif
      }
  }
  else
  {
    [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
  }
}

//-----------------------------------------------------------------------------------
- (int)addPackets:(uint8_t*)data size:(unsigned int)size
{
  //NSLog(@"resourceLoader: addPackets");
  CMTime cmSeconds = [_playerItem currentTime];
  _avLoader.currentTime = CMTimeGetSeconds(cmSeconds);

  if (data && size > 0)
    return write(mFileWriter, data, size);

  return size;
}

//-----------------------------------------------------------------------------------
- (void)play:(bool)state
{
  if (state)
    [_avplayer playImmediatelyAtRate:1.0];
  else
    [_avplayer pause];
}

//-----------------------------------------------------------------------------------
- (void)start
{
/*
  for (NSString *mime in [AVURLAsset audiovisualTypes])
    NSLog(@"AVURLAsset audiovisualTypes:%@", mime);

  for (NSString *mime in [AVURLAsset audiovisualMIMETypes])
    NSLog(@"AVURLAsset audiovisualMIMETypes:%@", mime);
*/
  // run on our own serial queue, keeps main thread from stalling us
  // and lets us do long sleeps waiting for data without stalling main thread.
  dispatch_sync(_serialQueue, ^{
    std::string tmpBufferFile = [NSTemporaryDirectory() UTF8String];
    tmpBufferFile += "avaudio.ec3";
    unlink(tmpBufferFile.c_str());
    mFileWriter = open(tmpBufferFile.c_str(), O_CREAT | O_WRONLY | O_APPEND | O_SYNC, 00666);
    fsync(mFileWriter);

    NSString *extension = @"ec3";
    // needs leading dir ('fake') or pathExtension in resourceLoader will fail
    NSMutableString *url = [NSMutableString stringWithString:@"mrmc_streaming://fake/dummy."];
    [url appendString:extension];
    NSURL *streamURL = [NSURL URLWithString: url];
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:streamURL options:nil];
    _avLoader = [[AudioResourceLoader alloc] initWithFrameBytes:_frameSize];
    [asset.resourceLoader setDelegate:_avLoader queue:_serialQueue];

    _playerItem = [AVPlayerItem playerItemWithAsset:asset];
    [_playerItem addObserver:self forKeyPath:@"status" options:NSKeyValueObservingOptionNew context:nil];

    [_avplayer replaceCurrentItemWithPlayerItem:_playerItem];
    _avplayer.actionAtItemEnd = AVPlayerActionAtItemEndNone;
    _avplayer.automaticallyWaitsToMinimizeStalling = NO;
    _avplayer.currentItem.canUseNetworkResourcesForLiveStreamingWhilePaused = YES;
  });
}

//-----------------------------------------------------------------------------------
- (bool)loaded
{
  return _loadedFlag;
}

//-----------------------------------------------------------------------------------
- (void)flush
{
  close(mFileWriter), mFileWriter = 0;

  [_avplayer replaceCurrentItemWithPlayerItem:nil];
  [_playerItem removeObserver:self forKeyPath:@"status"];
  [_playerItem.asset cancelLoading];
  _playerItem = nullptr;
  [_avLoader abort];
  _avLoader = nullptr;
  [self start];
}

//-----------------------------------------------------------------------------------
- (double)currentTime
{
  CMTime cmSeconds = [_playerItem currentTime];
  double sink_s = CMTimeGetSeconds(cmSeconds);
  _avLoader.currentTime = sink_s;
  if (sink_s > 0.0)
    return sink_s;
  else
    return 0.0;
}

//-----------------------------------------------------------------------------------
- (double)bufferedTime
{
  CMTime cmSeconds = [_playerItem currentTime];
  _avLoader.currentTime = CMTimeGetSeconds(cmSeconds);
  double seconds = _avLoader.bufferedTime - _avLoader.currentTime;
  if (seconds < 0.0)
    seconds = 0.0;

  return seconds;
}

@end

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
typedef struct OpaqueAVAudioSinkSession
{
  int frameSize;
  AVPlayerSink *avsink;
}
AVAudioSinkSessionRef;

extern "C" AVAudioSinkSessionRef* avaudiosink_open(const int framebytes)
{
  AVAudioSinkSessionRef *avref = NULL;
  avref = (AVAudioSinkSessionRef*)calloc(1, sizeof(AVAudioSinkSessionRef));
  avref->frameSize = framebytes;
  avref->avsink = [[AVPlayerSink alloc] initWithFrameSize:framebytes];
  if (avref->avsink)
    [avref->avsink start];
  else
    free(avref), avref = nullptr;
  return avref;
};

// close, going away
extern "C" void avaudiosink_close(AVAudioSinkSessionRef *avref)
{
  if (avref && avref->avsink)
  {
    [avref->avsink close];
    avref->avsink = nullptr;
  }
  free(avref);
};

// add frame packets to sink buffers
extern "C" int avaudiosink_write(AVAudioSinkSessionRef* avref, const uint8_t *buf, int len)
{
  int written = 0;
  if (avref && avref->avsink)
    written = [avref->avsink addPackets:(uint8_t*)buf size:len];
  return written;
};

// start/stop audio playback
extern "C" void avaudiosink_play(AVAudioSinkSessionRef* avref, const int playpause)
{
  if (avref && avref->avsink)
    [avref->avsink play:playpause];
};

// flush audio playback buffers
extern "C" void avaudiosink_flush(AVAudioSinkSessionRef* avref)
{
  if (avref && avref->avsink)
    [avref->avsink flush];
};

// return true when sink is ready to output audio after filling
extern "C" int avaudiosink_ready(AVAudioSinkSessionRef* avref)
{
  if (avref && avref->avsink)
    return [avref->avsink loaded] == true;
  return 0;
};

// time in seconds of when sound hits your ears
extern "C" double avaudiosink_timeseconds(AVAudioSinkSessionRef* avref)
{
  if (avref && avref->avsink)
    return [avref->avsink currentTime];
  return 0.0;
};

// delay in seconds of adding data before it hits your ears
extern "C" double avaudiosink_delayseconds(AVAudioSinkSessionRef* avref)
{
  if (avref && avref->avsink)
    return [avref->avsink bufferedTime];
  return 0.0;
};

// alternative delay_s method
extern "C" double avaudiosink_delay2seconds(AVAudioSinkSessionRef* avref)
{
  if (avref && avref->avsink)
    return [avref->avsink bufferedTime];
  return 0.0;
};

// sink error seconds
extern "C" double avaudiosink_errorseconds(AVAudioSinkSessionRef* avref)
{
  return 0.0;
};

extern "C" double avaudiosink_mindelayseconds(AVAudioSinkSessionRef* avref)
{
  return 2.5;
};

extern "C" double avaudiosink_maxdelayseconds(AVAudioSinkSessionRef* avref)
{
  return 4.0;
};

