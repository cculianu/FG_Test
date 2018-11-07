#include "Frame.h"

extern int FrameTypeId;
int FrameTypeId = qRegisterMetaType<Frame>(); ///< make sure Frame can be used in signals/slots
