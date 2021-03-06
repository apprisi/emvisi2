/*
   emvisi2 makes background subtraction robust to illumination changes.
   Copyright (C) 2008 Julien Pilet, Christoph Strecha, and Pascal Fua.

   This file is part of emvisi2.

   emvisi2 is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   emvisi2 is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with emvisi2.  If not, see <http://www.gnu.org/licenses/>.


   For more information about this code, see our paper "Making Background
   Subtraction Robust to Sudden Illumination Changes".
*/
/*
 * Normalized cross-correlation computation using integral images.
 * Julien Pilet, 2008.
 */
#include <pmmintrin.h>
#include "fwncc.h"

//#define FWNCC_MAIN

FNcc::FNcc() {
	width=0;
	integral=0;
	ncc=0;
	dint=0;
}

FNcc::~FNcc() {
	if (integral) {
		delete[] integral;
		delete[] ncc;
	}
	if (dint) delete[] dint;
}

void FNcc::setModel(const cv::Mat b, const cv::Mat mask) {
        this->mask = mask;
	if (!mask.empty()) {
		mask_integral = cv::Mat(mask.rows+1, mask.cols+1, CV_32SC1);
                mask_integral = cv::Scalar::all(0);
		for (int y=0; y<mask.rows;y++) {
			int *m = mask_integral.ptr<int>(y+1) + 1;
			int *mup = mask_integral.ptr<int>(y) + 1;
			for (int x=0; x<mask.cols; x++) {
				m[x] = mup[x]-mup[x-1]+m[x-1] +
					(mask.at<unsigned char>(y, x) ? 1:0);
			}
		}
	}

	this->b = b;

	assert(b.channels() == 1);
	assert(b.depth() == CV_8U);

	width = b.cols+1;
	height = b.rows+1;

	if (integral) delete[] integral;
	integral = new CSum[width*height];
	if (ncc) delete[] ncc;
	ncc = new CSumf[width*height];
	if (dint) delete[] dint;
	dint = new DSum[width*height];

	memset(dint, 0, sizeof(DSum)*width);
	memset(integral, 0, sizeof(CSum)*width);

	CSum *p= integral + width + 1;

	for (int y=0; y<b.rows; y++, p+=width) {
		memset(p-1,0, sizeof(CSum));

		unsigned char *line = this->b.ptr<unsigned char>(y);
		unsigned char *m=0;
		if (!mask.empty()) {
                  m = this->mask.ptr<unsigned char>(y);
                }

		for (int x=0; x<b.cols; x++) {
			if (m && m[x]==0) line[x]=0;
			p[x].b = p[x-1].b + line[x];
			p[x].b2 = p[x-1].b2 + line[x]*line[x];
		}
		for (int x=0; x<b.cols; x++) {
			p[x].b += p[x-width].b;
			p[x].b2 += p[x-width].b2;
		}
	}
}

void FNcc::setImage(cv::Mat a) {

	this->a=a;

	assert(a.cols+1 == width && a.rows+1 == height);
	assert(a.channels()==1);
	assert(a.depth() == CV_8U);

	CSum *p= integral + width + 1;

	for (int y=0; y<a.rows; y++, p+=width) {

		unsigned char *aline = a.ptr<unsigned char>(y);
		unsigned char *bline = b.ptr<unsigned char>(y);
		unsigned char *m=0;
		if (!mask.empty()) {
                  m = mask.ptr<unsigned char>(y);
                }

		for (int x=0; x<a.cols; x++) {
			unsigned char ax = ((m && m[x]==0) ? 0 : aline[x]);
			p[x].a_a2 = _mm_add_pd(
					_mm_sub_pd(p[x-1].a_a2, p[x-width-1].a_a2), 
					_mm_add_pd(p[x-width].a_a2, 
						_mm_set_pd(ax*ax,ax)));
			p[x].ab = p[x-1].ab - p[x-width-1].ab + p[x-width].ab + ax*bline[x];
		}
	}
}
	
static inline float texeval(float a, float b, float n) {
	//return a*b*n*n;
	return (a+b)*n;
}

#define FMAX(a,b) (a>b ? a:b)

void FNcc::computeNcc(int winSize, cv::Mat dst, cv::Mat sumvar) {
	if (!mask.empty()) computeNcc_mask(winSize, dst, sumvar);
	else computeNcc_nomask(winSize, dst, sumvar);
}

void FNcc::computeNcc_nomask(int winSize, cv::Mat dst, cv::Mat sumvar) {

	assert(dst.empty() || (dst.cols+1 == width && dst.rows+1 == height));
	assert(sumvar.empty() || sumvar.type() == CV_32FC1);
	const int w2 = winSize/2;

//pragma omp parallel for 
	for (int y=0; y<height-1; y++) {
		CSumf *d = ncc + y*width;
		int yup = MAX(0,y-w2-1);
		int ydown = MIN(height-1,y+w2);
		CSum *up = integral + yup*width;
		CSum *down = integral + ydown*width;

		float *dline = 0;
		if (!dst.empty()) dline = dst.ptr<float>(y);
		float *sv=0;
		if (!sumvar.empty()) sv = sumvar.ptr<float>(y);

		CSum *upl = up - w2 - 1;
		CSum *upr = up + w2;
		CSum *downl = down -w2 -1;
		CSum *downr = down +w2;

		float sqrtN = 1.0f/float(2*w2+1);
		float N=sqrtN*sqrtN;

		for (int x=0; x<w2+1; x++) {
			//d[x,y] = i[x+w2, y+w2] - i[x-w2-1, y+w2] - i[x+w2,y-w2-1] + i[x-w2-1,y-w2-1]
			
			N = 1.0f/MAX((x+w2)*(ydown-yup),1);
			sqrtN = sqrtf(N);

			CSum sd;
			sd.setaddsub4(up[0], upr[x], downr[x], down[0]);
			CSumf s;

			s = sd;

			float num = s.ab - N*s.a*s.b;
			float vara = s.a2 - N*s.a*s.a;
			float varb = s.b2 - N*s.b*s.b;
			d[x] = s;
			if (sv) {
				vara = sqrtf(FMAX(0,vara));
				varb = sqrtf(FMAX(0,varb));
				if (dline) {
					float f = vara*varb;
					dline[x] = (f>1 ? num/(vara*varb) : 0);
				}
				sv[x] = texeval(vara,varb, sqrtN); //(vara+varb)*sqrtN;
			} else if (dline) {	
				float f = vara*varb;
				if (f<1) dline[x]=0;
				else
					_mm_store_ss(dline+x, _mm_mul_ss(_mm_set_ss(num), _mm_rsqrt_ss(_mm_set_ss(f))));
			}

		}

		N = 1.0f/((float)(ydown-yup)*(w2*2+1));
		sqrtN = sqrtf(N);
		for (int x=w2+1; x<dst.cols-w2; x++) {

			//d[x,y] = i[x+w2, y+w2] - i[x-w2-1, y+w2] - i[x+w2,y-w2-1] + i[x-w2-1,y-w2-1]
			CSum sd;
			sd.setaddsub4(upl[x], upr[x], downr[x], downl[x]);
			const CSum s = sd;
			d[x] = s;

			const float num = s.ab - N*s.a*s.b;
			float vara = s.a2 - N*s.a*s.a;
			float varb = s.b2 - N*s.b*s.b;
			if (sv) {
				vara = sqrtf(FMAX(0,vara));
				varb = sqrtf(FMAX(0,varb));
				if (dline) {
					float f = vara*varb;
					dline[x] = (f>1 ? num/(vara*varb) : 0);
				}
				//sv[x] = (vara+varb)*sqrtN;
				sv[x] = texeval(vara,varb, sqrtN); //(vara+varb)*sqrtN;
			} else if (dline) {	
				const float f = vara*varb;
				if (f<1) dline[x]=0;
				else
				_mm_store_ss(dline+x, _mm_mul_ss(_mm_set_ps1(num), _mm_rsqrt_ss(_mm_set_ps1(f))));
			}
		}
		for (int x=dst.cols-w2; x<dst.cols; x++) {

			//d[x,y] = i[x+w2, y+w2] - i[x-w2-1, y+w2] - i[x+w2,y-w2-1] + i[x-w2-1,y-w2-1]

			N = 1.0f/MAX((ydown-yup)*(dst.cols-(x-w2-1)),1);
			sqrtN = sqrtf(N);

			CSum sd;
			sd.setaddsub4(upl[x],up[dst.cols], down[dst.cols], downl[x]);
			CSum s=sd;
			d[x]=s;

			float num = s.ab - N*s.a*s.b;
			float vara = s.a2 - N*s.a*s.a;
			float varb = s.b2 - N*s.b*s.b;
			if (sv) {
				vara = sqrtf(FMAX(0,vara));
				varb = sqrtf(FMAX(0,varb));
				if (dline) {
					float f = vara*varb;
					dline[x] = (f>1 ? num/(vara*varb) : 0);
				}
				//sv[x] = (vara+varb)*sqrtN;
				sv[x] = texeval(vara,varb, sqrtN); //(vara+varb)*sqrtN;
			} else if (dline) {	
				float f = vara*varb;
				if (f<1) dline[x]=0;
				else
				_mm_store_ss(dline+x, _mm_mul_ss(_mm_set_ps1(num), _mm_rsqrt_ss(_mm_set_ps1(f))));
			}

		}

	}
}

void FNcc::computeNcc_mask(int winSize, cv::Mat dst, cv::Mat sumvar) {

	assert(dst.empty() || (dst.cols+1 == width && dst.rows+1 == height));
	assert(sumvar.empty() || sumvar.depth()==CV_32F);
	assert(mask.depth()==CV_8U);
	assert(mask.cols == (width-1) && mask.rows == (height-1));
	assert(mask.channels()==1);
	assert(mask_integral.depth()==CV_32S && mask_integral.channels()==1);

	const int w2 = winSize/2;


//#pragma omp parallel for
	for (int y=0; y<height-1; y++) {
		CSumf *d = ncc + y*width;
		int yup = MAX(0,y-w2-1);
		int ydown = MIN(height-1,y+w2);
		CSum *up = integral + yup*width;
		CSum *down = integral + ydown*width;

		float *dline = 0;
		if (!dst.empty()) dline = dst.ptr<float>(y);
		float *sv=0;
		if (!sumvar.empty()) sv = sumvar.ptr<float>(y);

		CSum *upl = up - w2 - 1;
		CSum *upr = up + w2;
		CSum *downl = down -w2 -1;
		CSum *downr = down +w2;

		float sqrtN = 1.0f/float(2*w2+1);
		float N=sqrtN*sqrtN;

		unsigned char *m = mask.ptr<unsigned char>(y);

		int *mask_up = mask_integral.ptr<int>(yup);
		int *mask_down = mask_integral.ptr<int>(ydown);

		for (int x=0; x<w2+1; x++) {
			if (m[x]==0) {
				if(dline) dline[x]=0;
				if(sv) sv[x]=0;
				continue;
			}
			//d[x,y] = i[x+w2, y+w2] - i[x-w2-1, y+w2] - i[x+w2,y-w2-1] + i[x-w2-1,y-w2-1]
			
			int n = mask_up[0] - mask_down[0] - mask_up[x+w2] + mask_down[x+w2];
			assert(n>0);
			N = 1.0f/n;
			sqrtN = sqrtf(N);

			CSum sd;
			sd.setaddsub4(up[0], upr[x], downr[x], down[0]);
			CSumf s;

			s = sd;

			float num = s.ab - N*s.a*s.b;
			float vara = s.a2 - N*s.a*s.a;
			float varb = s.b2 - N*s.b*s.b;
			d[x] = s;
			if (sv) {
				vara = sqrtf(FMAX(0,vara));
				varb = sqrtf(FMAX(0,varb));
				if (dline) {
					float f = vara*varb;
					dline[x] = (f>1 ? num/(vara*varb) : 0);
				}
				//sv[x] = (vara+varb)*sqrtN;
				sv[x] = texeval(vara,varb, sqrtN); //(vara+varb)*sqrtN;
			} else if (dline) {	
				float f = vara*varb;
				if (f<1) dline[x]=0;
				else
					_mm_store_ss(dline+x, _mm_mul_ss(_mm_set_ss(num), _mm_rsqrt_ss(_mm_set_ss(f))));
			}

		}

		N = 1.0f/((float)(ydown-yup)*(w2*2+1));
		sqrtN = sqrtf(N);
		for (int x=w2+1; x<dst.cols-w2; x++) {

			if (m[x]==0) {
				if(dline) dline[x]=0;
				if(sv) sv[x]=0;
				continue;
			}
			//d[x,y] = i[x+w2, y+w2] - i[x-w2-1, y+w2] - i[x+w2,y-w2-1] + i[x-w2-1,y-w2-1]
			
			int n = mask_up[x-w2-1] - mask_down[x-w2-1] - mask_up[x+w2] + mask_down[x+w2];
			assert(n>0);
			N = 1.0f/n;
			sqrtN = sqrtf(N);
			CSum sd;
			sd.setaddsub4(upl[x], upr[x], downr[x], downl[x]);
			const CSum s = sd;
			d[x] = s;

			const float num = s.ab - N*s.a*s.b;
			float vara = s.a2 - N*s.a*s.a;
			float varb = s.b2 - N*s.b*s.b;
			if (sv) {
				vara = sqrtf(FMAX(0,vara));
				varb = sqrtf(FMAX(0,varb));
				if (dline) {
					float f = vara*varb;
					dline[x] = (f>1 ? num/(vara*varb) : 0);
				}
				//sv[x] = (vara+varb)*sqrtN;
				sv[x] = texeval(vara,varb, sqrtN); //(vara+varb)*sqrtN;
			} else if (dline) {	
				const float f = vara*varb;
				if (f<1) dline[x]=0;
				else
				_mm_store_ss(dline+x, _mm_mul_ss(_mm_set_ps1(num), _mm_rsqrt_ss(_mm_set_ps1(f))));
			}
		}
		for (int x=dst.cols-w2; x<dst.cols; x++) {

			if (m&&(m[x]==0)) {
				if(dline) dline[x]=0;
				if(sv) sv[x]=0;
				continue;
			}
			//d[x,y] = i[x+w2, y+w2] - i[x-w2-1, y+w2] - i[x+w2,y-w2-1] + i[x-w2-1,y-w2-1]

			int n = mask_up[x-w2-1] - mask_down[x-w2-1] - mask_up[dst.cols] + mask_down[dst.cols];
			assert(n>0);
			N = 1.0f/n;
			sqrtN = sqrtf(N);

			CSum sd;
			sd.setaddsub4(upl[x],up[dst.cols], down[dst.cols], downl[x]);
			CSum s=sd;
			d[x]=s;

			float num = s.ab - N*s.a*s.b;
			float vara = s.a2 - N*s.a*s.a;
			float varb = s.b2 - N*s.b*s.b;
			if (sv) {
				vara = sqrtf(FMAX(0,vara));
				varb = sqrtf(FMAX(0,varb));
				if (dline) {
					float f = vara*varb;
					dline[x] = (f>1 ? num/(vara*varb) : 0);
				}
				//sv[x] = (vara+varb)*sqrtN;
				sv[x] = texeval(vara,varb, sqrtN); //(vara+varb)*sqrtN;
			} else if (dline) {	
				float f = vara*varb;
				if (f<1) dline[x]=0;
				else
				_mm_store_ss(dline+x, _mm_mul_ss(_mm_set_ps1(num), _mm_rsqrt_ss(_mm_set_ps1(f))));
			}

		}

	}
}

FNccMC::FNccMC() {
}

FNccMC::~FNccMC() {
}

void FNccMC::setModel(const cv::Mat im, const cv::Mat mask) 
{
	assert(im.depth() == CV_8U);
	assert(im.channels() <= 3);
	for (int i=0; i<im.channels(); i++) {
		a[i] = cv::Mat(im.size(), CV_8UC1);
		b[i] = cv::Mat(im.size(), CV_8UC1);
	}
        cv::split(im, b);

#pragma omp parallel for
	for (int i=0; i<im.channels(); i++) {
		ncc[i].setModel(b[i], mask);
	}
}

void FNccMC::setImage(const cv::Mat im)
{
	assert(im.depth() == CV_8U);
	assert(im.channels() <= 3);

        cv::split(im, a);

#pragma omp parallel for
	for (int i=0; i<im.channels(); i++) {
		ncc[i].setImage(a[i]);
	}
}

void FNccMC::computeNcc(int windowSize, cv::Mat dst, cv::Mat sumvar)
{
#pragma omp parallel for
	for (int i=0; i< dst.channels(); i++) {
		tmp_flt1[i] = cv::Mat(dst.size(), CV_32FC1);
		if (!sumvar.empty())
			tmp_flt2[i] = cv::Mat(dst.size(), CV_32FC1);

		ncc[i].computeNcc(windowSize, tmp_flt1[i],
                                  (!sumvar.empty() ? tmp_flt2[i]: cv::Mat()));
	}
	merge(tmp_flt1, dst);
	if (!sumvar.empty())
		merge(tmp_flt2, sumvar);
}

void FNccMC::merge(cv::Mat *src, cv::Mat dst)
{
	if (dst.channels()>1) {
          cv::merge(src, 3, dst);
	} else {
		for (int i=1; i<dst.channels(); i++)
                  src[i] = src[0] + src[0];
                src[0].convertTo(dst, dst.type(), 1.0/dst.channels());
	}
}

/**** Weighted NCC ****/

FWNcc::FWNcc() {
}

FWNcc::~FWNcc() {
}

void FWNcc::prepare(cv::Mat a, cv::Mat b, cv::Mat w) {

	assert(a.cols == b.cols);
	assert(a.rows == b.rows);
	assert(a.channels() == 1);
	assert(b.channels() == 1);
	assert(a.depth() == 8);
	assert(b.depth() == 8);

	integral = cv::Mat(a.rows + 1, NSUMS*(a.cols+1), CV_64FC1);
        integral = cv::Scalar::all(0);

	for (int y=0; y<a.rows;y++) {

		unsigned char *la = a.ptr<unsigned char>(y);
		unsigned char *lb = b.ptr<unsigned char>(y);
		float *lw = 0;
		if (!w.empty()) {
                  lw = w.ptr<float>(y);
                }
		integral_type *sum = integral.ptr<integral_type>(y+1) + NSUMS;
		integral_type *upsum = integral.ptr<integral_type>(y) + NSUMS;

		for (int x=-NSUMS; x<0; x++) sum[x]=0;

			for (int x=0; x<a.cols; x++) {
				float va = la[x];
				float vb = lb[x];
				float vw = (lw?lw[x]:1);
				int x8 = x*NSUMS;

				sum[x8+SUM_A] = sum[x8-NSUMS+SUM_A] + va;
				sum[x8+SUM_B] = sum[x8-NSUMS+SUM_B] + vb;
				sum[x8+SUM_W] = sum[x8-NSUMS+SUM_W] + vw;
				sum[x8+SUM_WA] = sum[x8-NSUMS+SUM_WA] + va*vw;
				sum[x8+SUM_WB] = sum[x8-NSUMS+SUM_WB] + vb*vw;
				sum[x8+SUM_WAB] = sum[x8-NSUMS+SUM_WAB] + va*vb*vw;
				sum[x8+SUM_WA2] = sum[x8-NSUMS+SUM_WA2] + va*va*vw;
				sum[x8+SUM_WB2] = sum[x8-NSUMS+SUM_WB2] + vb*vb*vw;
				sum[x8+SUM_A2] = sum[x8-NSUMS+SUM_A2] + va*va;
				sum[x8+SUM_B2] = sum[x8-NSUMS+SUM_B2] + vb*vb;
				sum[x8+SUM_AB] = sum[x8-NSUMS+SUM_AB] + va*vb;

			}

		int n = a.cols*NSUMS;
		for (int x=0; x<n; x++) {
			sum[x] +=  upsum[x];
		}
	}
}

void FWNcc::compute(int winSize, cv::Mat dst)
{

	int w = winSize/2;
#pragma omp parallel for
	for (int y=0; y<dst.rows; y++) {

		int top = MAX(y-w,0);
		int bot = MIN(y+w, dst.rows);
		for (int x = 0; x<w; x++) 
                  dst.at<float>(y, x) = correl( 0,top, x+w,bot,x,y);
		for (int x = w; x<dst.cols-w; x++) 
			dst.at<float>(y, x) = correl( x-w,top, x+w,bot,x,y);
		for (int x = dst.cols-w; x<dst.cols; x++) 
			dst.at<float>(y, x) = correl( x-w,top, dst.cols,bot,x,y);
	}
}

inline void FWNcc::fetchRect(int x1, int y1, int x2, int y2, float s[NSUMS]) 
{
	integral_type *tl = integral.ptr<integral_type>(y1) + x1*NSUMS;
	integral_type *tr = integral.ptr<integral_type>(y1) + x2*NSUMS;
	integral_type *bl = integral.ptr<integral_type>(y2) + x1*NSUMS;
	integral_type *br = integral.ptr<integral_type>(y2) + x2*NSUMS;
	for (int i=0; i<NSUMS; i++)
		s[i] = (float)(tl[i]-tr[i]-bl[i]+br[i]);
}

static inline float InvSqrt(float x)
{
#if SSE2
	return _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x)));
#else
	return 1/sqrtf(x);
#endif
}


void FWNcc::correl(float area, float s[NSUMS], float *cw, float *cx) 
{
	float norm = 1.0f/area;
	float avgA = norm*s[SUM_A];
	float avgB = norm*s[SUM_B];

	if (cw) {
		float num = s[SUM_WAB] - avgB*s[SUM_WA] - avgA*s[SUM_WB] + avgA*avgB*s[SUM_W];
		float twa = (s[SUM_WA2]-2*avgA*s[SUM_WA]+avgA*avgA*s[SUM_W]);
		float twb = (s[SUM_WB2]-2*avgB*s[SUM_WB]+avgB*avgB*s[SUM_W]);

		*cw = num*InvSqrt(twa*twb);
	}

	if (cx) {
		float xab = s[SUM_AB] - s[SUM_WAB];
		float xa = s[SUM_A] - s[SUM_WA];
		float xb = s[SUM_B] - s[SUM_WB];
		float x = area - s[SUM_W];
		float xa2 = s[SUM_A2]-s[SUM_WA2];
		float xb2 = s[SUM_B2]-s[SUM_WB2];

		float txa = (float)(xa2-2*avgA*xa+avgA*avgA*x);
		float txb = (float)(xb2-2*avgB*xb+avgB*avgB*x);
		*cx = (xab - avgB*xa - avgA*xb +avgA*avgB*x)*InvSqrt(txa*txb);
	}
}

float FWNcc::correl(int x1, int y1, int x2, int y2, int centerx, int centery)
{
	float s[NSUMS];
	float cx[NSUMS];
	float se[NSUMS];
	float area = (y2-y1)*(x2-x1);
	fetchRect(x1,y1,x2,y2,s);
	fetchRect(centerx,centery,centerx+1,centery+1,cx);
	for (int i=0;i<NSUMS;i++)
		se[i]=s[i]-cx[i];

	float sw = se[SUM_W];
	float sx = area - se[SUM_W];
	float result;
	if (sw > area/3 && sx > area/3) {
		float cw,cx,cew,cex;
		correl(area, s,&cw,&cx);
		correl(area, se,&cew,&cex);

		float dw = sw*fabs(cw-cew);
		float dx = sx*fabs(cx-cex);
		if (dw<dx)
			result = cw;
		else
			result = cx;
	} else if (sw>sx) {
		correl(area, s, &result, 0);
	} else {
		correl(area, s, 0, &result);
	}
	return MAX(result,0);
}

