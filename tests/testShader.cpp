// This file may be redistributed and modified only under the terms of
// the GNU General Public License (See COPYING for details).
// Copyright (C) 2003 Alistair Riddoch

#include <Mercator/Segment.h>
#include <Mercator/Surface.h>
#include <Mercator/FillShader.h>

int main()
{
    Mercator::Segment segment;
    Mercator::Surface surface(segment);

    Mercator::FillShader fill_shader;

    fill_shader.shade(surface);
}
