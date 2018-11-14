#include "FrameGenerator.h"
#include "Util.h"

FrameGenerator::FrameGenerator()
{
    thr.setObjectName("Frame Generator");
    postLambdaSync([this] {
        // run in thread...

        ps = new PerSec(this);
        connect(ps, SIGNAL(perSec(double)), this, SIGNAL(fps(double)));
        connect(this, &FrameGenerator::generatedFrame, this, [this] {
            ps->mark();
        });
    });
}

FrameGenerator::~FrameGenerator()
{
    postLambdaSync([this]{
        delete ps; ps = nullptr;
    });
}
