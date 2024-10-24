set(SOURCES DemuxMultiSource.cpp
            DVDDemux.cpp
            DVDDemuxBXA.cpp
            DVDDemuxCC.cpp
            DVDDemuxCDDA.cpp
            DVDDemuxClient.cpp
            DVDDemuxFFmpeg.cpp
            DVDDemuxUtils.cpp
            DVDDemuxVobsub.cpp
            DVDStreamMuxer.cpp
            DVDMvcStreamMuxer.cpp
            DVDFactoryDemuxer.cpp)

set(HEADERS DemuxMultiSource.h
            DVDDemux.h
            DVDDemuxBXA.h
            DVDDemuxCC.h
            DVDDemuxCDDA.h
            DVDDemuxClient.h
            DVDDemuxFFmpeg.h
            DVDDemuxUtils.h
            DVDDemuxVobsub.h
            DVDStreamMuxer.h
            DVDMvcStreamMuxer.h
            DVDFactoryDemuxer.h)

core_add_library(dvddemuxers)
