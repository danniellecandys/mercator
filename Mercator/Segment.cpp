// This file may be redistributed and modified only under the terms of
// the GNU General Public License (See COPYING for details).
// Copyright (C) 2003 Alistair Riddoch

#include <Mercator/Segment.h>
#include <Mercator/Terrain.h>
#include <Mercator/TerrainMod.h>
#include <Mercator/BasePoint.h>
#include <iostream>
#include <cmath>

namespace Mercator {

//a couple of helper classes
//these interpolate between points or across a quad
class LinInterp {
  private:
    int m_size;
  public:
    float ep1, ep2;
    float calc(int loc) {
        return ((m_size-loc) * ep1 + loc * ep2);
    }
    LinInterp(int size,float l, float h) : m_size(size), ep1(l/size), ep2(h/size) {} 
};

class QuadInterp {
  private:
    int m_size;
  public:
    float ep1, ep2, ep3, ep4;
    float calc(int locX, int locY) {
	return (( ep1*(m_size-locX) + ep2 * locX) * (m_size-locY) +
	        ( ep4*(m_size-locX) + ep3 * locX) * (locY) ) / m_size;
    }
    QuadInterp(int size,float e1, float e2, float e3, float e4)
        : m_size(size), ep1(e1/size), ep2(e2/size), ep3(e3/size), ep4(e4/size) {} 
};	

Segment::Segment(int res) : m_res(res), m_points(new float[(res+1) * (res+1)]),
                            m_normals(0), m_max(0.f), m_min(0.0f), m_validPt(false),
                            m_validNorm(false)
{
}

Segment::~Segment()
{
    delete m_points;
    if (m_normals != 0) {
        delete m_normals;
    }
    clearMods();
}

void Segment::populate(const Matrix<2, 2, BasePoint> & base)
{
    fill2d(m_res, base(0, 0), base(1, 0), 
                  base(1, 1), base(0, 1));

    for (ModList::iterator I=m_modList.begin(); I!=m_modList.end(); ++I) {
        applyMod(*I);
    }

    m_validPt = true;
}

void Segment::populateNormals()
{
    if (m_normals == 0) {
        m_normals = new float[(m_res+1) * (m_res+1) * 3];
    }

    float * np = m_normals;
    
    // Fill in the damn normals
    float h1,h2,h3,h4;
    for (int j = 1; j < m_res; ++j) {
        for (int i = 1; i < m_res; ++i) {
           h1 = get(i - 1, j);
           h2 = get(i, j + 1);
           h3 = get(i + 1, j);
           h4 = get(i, j - 1);
	   
           // Caclulate the normal vector.
           np[j * (m_res+1) * 3 + i * 3]     = h1 - h3;
           np[j * (m_res+1) * 3 + i * 3 + 1] = h2 - h4;
           np[j * (m_res+1) * 3 + i * 3 + 2] = 1.0;
        }
    }

    //top and bottom boundary
    for (int i=0; i <= m_res; ++i) {
        h1 = get(i, 0);
        h2 = get(i, 1);
        h3 = get(i + 1, 1);
        h4 = get(i + 1, 0);
	
        np[i * 3]     = (h2-h3 + h1-h4) / 2.0;
        np[i * 3 + 1] = (h1-h2 + h4-h3) / 2.0;
        np[i * 3 + 2] = 1.0;
 
        h1 = get(i, m_res - 1);
        h2 = get(i, m_res);
        h3 = get(i + 1, m_res);
        h4 = get(i + 1, m_res -1);
	
        np[m_res * (m_res+1) * 3 + i * 3]     = (h2-h3 + h1-h4) / 2.0;
        np[m_res * (m_res+1) * 3 + i * 3 + 1] = (h1-h2 + h4-h3) / 2.0;
        np[m_res * (m_res+1) * 3 + i * 3 + 2] = 1.0;
    }
    
    //left and right boundary
    for (int j=1; j < m_res; ++j) {
        h1 = get(0, j);
        h2 = get(0, j - 1);
        h3 = get(1, j - 1);
        h4 = get(1, j);
	
        np[j * (m_res + 1) * 3]     = (h2-h3 + h1-h4) / 2.0;
        np[j * (m_res + 1) * 3 + 1] = (h1-h2 + h4-h3) / 2.0;
        np[j * (m_res + 1) * 3 + 2] = 1.0;
 
        h1 = get(m_res - 1, j);
        h2 = get(m_res - 1, j - 1);
        h3 = get(m_res, j - 1);
        h4 = get(m_res, j);

        np[j * (m_res+1) * 3 + m_res * 3]     = (h2-h3 + h1-h4) / 2.0;
        np[j * (m_res+1) * 3 + m_res * 3 + 1] = (h1-h2 + h4-h3) / 2.0;
        np[j * (m_res+1) * 3 + m_res * 3 + 2] = 1.0;
    }

    m_validNorm = true;
}

// rand num between -0.5...0.5
inline float randHalf()
{
    return (float) rand() / RAND_MAX - 0.5f;
}


// quasi-Random Midpoint Displacement algorithm
float Segment::qRMD(float nn, float fn, float ff, float nf,
                    float roughness, float falloff, int depth) const
{
    float heightDifference = std::max(std::max(nn, fn), std::max(nf, ff)) -
                             std::min(std::min(nn, fn), std::min(nf, ff));
 
    return ((nn+fn+ff+nf)/4.f) + randHalf() * roughness * heightDifference / (1.f+::pow(depth,falloff));
}

// 1 dimensional midpoint displacement fractal
// size must be a power of 2
// falloff is the decay of displacement as the fractal is refined
// array is size+1 long. array[0] and array[size] are filled
//      with the control points for the fractal
void Segment::fill1d(int size, const BasePoint& l, const BasePoint &h, 
		     float *array) const
{
    array[0] = l.height();
    array[size] = h.height();
    LinInterp li(size, l.roughness(), h.roughness());
   
    // seed the RNG.
    // The RNG is seeded only once for the line and the seed is based on the
    // two endpoints -because they are the common parameters for two adjoining
    // tiles
    srand((l.seed() * 1000 + h.seed()));

    // stride is used to step across the array in a deterministic fashion
    // effectively we do the 1/2  point, then the 1/4 points, then the 1/8th
    // points etc. this has to be the same order every time because we call
    // on the RNG at every point 
    int stride = size/2;

    // depth is used to indicate what level we are on. the displacement is
    // reduced each time we traverse the array. because this is really the
    // edge of a 2d array, the depth decrement is doubled within the loop
    int depth=1;
 
    while (stride) {
        for (int i=stride;i<size;i+=stride*2) {
            float hh = array[i-stride];
            float lh = array[i+stride];
	    float hd = fabs(hh-lh);
            float roughness = li.calc(i);

	    //eliminate the problem where hd is nearly zero, leaving a flat section.
	    if ((hd*100.f) < roughness) {
	        hd+=0.05 * roughness;	    
	    }
	  
            array[i] = ((hh+lh)/2.f) + randHalf() * roughness  * hd / (1+::pow(depth,BasePoint::FALLOFF));
        }
        stride >>= 1;
        depth++;
    }
}

// 2 dimensional midpoint displacement fractal for a tile where
// edges are to be filled by 1d fractals.// size must be a power of 2
// array is size+1  * size+1 with the corners the control points.
void Segment::fill2d(int size, const BasePoint& p1, const BasePoint& p2, 
		               const BasePoint& p3, const BasePoint& p4)
{
    assert(m_points!=0);
    
    int line = size+1;
    
    // calculate the edges first. This is necessary so that segments tile
    // seamlessly note the order in which the edges are calculated and the
    // direction. opposite edges are calculated the same way (eg left->right)
    // so that the top of one tile matches the bottom of another, likewise
    // with sides.
    
    // temporary array used to hold each edge
    float edge[line];
    
    // calc top edge and copy into m_points
    fill1d(size,p1,p2,edge);
    for (int i=0;i<=size;i++) {
        m_points[0*line + i] = edge[i];
        checkMaxMin(edge[i]);
    }

    // calc left edge and copy into m_points
    fill1d(size,p1,p4,edge);
    for (int i=0;i<=size;i++) {
        m_points[i*line + 0] = edge[i];
        checkMaxMin(edge[i]);
    }
   
    // calc right edge and copy into m_points
    fill1d(size,p2,p3,edge);
    for (int i=0;i<=size;i++) {
        m_points[i*line + size] = edge[i];
        checkMaxMin(edge[i]);
    }

    // calc bottom edge and copy into m_points
    fill1d(size,p4,p3,edge);
    for (int i=0;i<=size;i++) {
        m_points[size*line + i] = edge[i];
        checkMaxMin(edge[i]);
    }
    
    // seed the RNG - this is the 5th and last seeding for the tile.
    // it was seeded once for each edge, now once for the tile.
    srand(p1.seed()*20 + p2.seed()*15 + p3.seed()*10 + p4.seed()*5);

    QuadInterp qi(size, p1.roughness(), p2.roughness(), p3.roughness(), p4.roughness());

    float f = BasePoint::FALLOFF;
    int depth=0;
    
    // center of m_points is done separately
    int stride = size/2;

    //float roughness = (p1.roughness+p2.roughness+p3.roughness+p4.roughness)/(4.0f);
    float roughness = qi.calc(stride, stride);
    m_points[stride*line + stride] = qRMD( m_points[0 * line + size/2],
                                        m_points[size/2*line + 0],
                                        m_points[size/2*line + size],
                                        m_points[size*line + size/2],
                                        roughness,
                                        f, depth);
                    

    checkMaxMin(m_points[stride*line + stride]);
    stride >>= 1;

    // skip across the m_points and fill in the points
    // alternate cross and plus shapes.
    // this is a diamond-square algorithm.
    while (stride) {
      //Cross shape - + contributes to value at X
      //+ . +
      //. X .
      //+ . +
      for (int i=stride;i<size;i+=stride*2) {
          for (int j=stride;j<size;j+=stride*2) {
	      roughness=qi.calc(i,j);
              m_points[j*line + i] = qRMD(m_points[(i-stride) + (j+stride) * (line)],
                                       m_points[(i+stride) + (j-stride) * (line)],
                                       m_points[(i+stride) + (j+stride) * (line)],
                                       m_points[(i-stride) + (j-stride) * (line)],
                                       roughness, f, depth);
              checkMaxMin(m_points[j*line + i]);
          }
      }
 
      depth++;
      //Plus shape - + contributes to value at X
      //. + .
      //+ X +
      //. + .
      for (int i=stride*2;i<size;i+=stride*2) {
          for (int j=stride;j<size;j+=stride*2) {
	      roughness=qi.calc(i,j);
              m_points[j*line + i] = qRMD(m_points[(i-stride) + (j) * (line)],
                                       m_points[(i+stride) + (j) * (line)],
                                       m_points[(i) + (j+stride) * (line)],
                                       m_points[(i) + (j-stride) * (line)], 
                                       roughness, f , depth);
              checkMaxMin(m_points[j*line + i]);
          }
      }
               
      for (int i=stride;i<size;i+=stride*2) {
          for (int j=stride*2;j<size;j+=stride*2) {
	      roughness=qi.calc(i,j);
              m_points[j*line + i] = qRMD(m_points[(i-stride) + (j) * (line)],
                                       m_points[(i+stride) + (j) * (line)],
                                       m_points[(i) + (j+stride) * (line)],
                                       m_points[(i) + (j-stride) * (line)],
                                       roughness, f, depth);
              checkMaxMin(m_points[j*line + i]);
          }
      }

      stride>>=1;
      depth++;
    }
}

// FIXME getHeightAndNormal seems a bit verbose
void Segment::getHeightAndNormal(float x, float y, float& h,
                                 WFMath::Vector<3> &normal) const
{
    // FIXME this ignores edges and corners
    assert(x < m_res);
    assert(x >= 0.0);
    assert(y < m_res);
    assert(y >= 0.0);
    
    // get index of the actual tile in the segment
    int tile_x = (int)floor(x);
    int tile_y = (int)floor(y);

    // work out the offset into that tile
    float off_x = x - tile_x;
    float off_y = y - tile_y;
 
    float h1=get(tile_x, tile_y);
    float h2=get(tile_x, tile_y+1);
    float h3=get(tile_x+1, tile_y+1);
    float h4=get(tile_x+1, tile_y);

    // square is broken into two triangles
    // top triangle |/
    if ((off_x - off_y) < 0) {
        normal = WFMath::Vector<3>(h2-h3, h1-h2, 1.0);
        h = h1 + (h3-h2) * off_x + (h2-h1) * off_y;
    } 
    // bottom triangle /|
    else {
        normal = WFMath::Vector<3>(h1-h4, h4-h3, 1.0);
        h = h1 + (h4-h1) * off_x + (h3-h4) * off_y;
    }
}

bool Segment::clipToSegment(const WFMath::AxisBox<2> &bbox, int &lx, int &hx,
                                                            int &ly, int &hy) 
{

    lx = (int)round(bbox.lowCorner()[0]); 
    if (lx > m_res) return false;
    if (lx < 0) lx = 0;
    
    hx = (int)round(bbox.highCorner()[1]); 
    if (hx < 0) return false;
    if (hx > m_res) hx = m_res;
    
    ly = (int)round(bbox.lowCorner()[0]); 
    if (ly > m_res) return false;
    if (ly < 0) ly = 0;
    
    hy = (int)round(bbox.highCorner()[1]); 
    if (hy < 0) return false;
    if (hy > m_res) hy = m_res;

    return true;
}

void Segment::addMod(TerrainMod *t) 
{
    m_modList.push_back(t);
    applyMod(t);
    //currently mods dont fix the normals
    m_validNorm = false;
}

void Segment::clearMods() 
{
    for (ModList::iterator I=m_modList.begin(); I!=m_modList.end(); ++I) {
        delete(*I);
    }
    m_modList.clear();
    m_validPt = false;
    m_validNorm = false;
}

void Segment::applyMod(TerrainMod *t) 
{
    int lx,hx,ly,hy;
    if (clipToSegment(t->bbox(), lx, hx, ly, hy)) {
        for (int i=ly;i<=hy;i++) {
            for (int j=lx;j<=hx;j++) {
                t->apply(m_points[i * (m_res + 1) + j], j, i);
            }
        }
    }

    //currently mods dont fix the normals
    m_validNorm = false;
}

} // namespace Mercator
