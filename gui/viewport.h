/*
	Copyright(c) 2010 Johannes Jordan <johannes.jordan@cs.fau.de>.

	This file may be licensed under the terms of of the GNU General Public
	License, version 3, as published by the Free Software Foundation. You can
	find it here: http://www.gnu.org/licenses/gpl.html
*/

#ifndef VIEWPORT_H
#define VIEWPORT_H

#include <multi_img.h>
#include <cv.h>
#include <QGLWidget>
#include <QGLBuffer>
#include <vector>
#include <QHash>
#include <QLabel>
#include <QTimer>
#include <limits>

struct Bin {
	Bin() {}
	Bin(const multi_img::Pixel& initial_means)
	 : weight(1.f), means(initial_means) {} //, points(initial_means.size()) {}

	inline void add(const multi_img::Pixel& p) {
		weight += 1.f;
		std::transform(means.begin(), means.end(), p.begin(), means.begin(),
					   std::plus<multi_img::Value>());
	}

	float weight;
	std::vector<multi_img::Value> means;
	QColor rgb;
	//QPolygonF points;
};

struct BinSet {
	BinSet(const QColor &c, int size)
		: label(c), totalweight(0.f),
		boundary(size, std::make_pair((int)255, (int)0)) {}
	QColor label;
	QHash<QByteArray, Bin> bins;
	float totalweight;
	std::vector<std::pair<int, int> > boundary;
};


class Viewport : public QGLWidget
{
	Q_OBJECT
public:
	Viewport(QWidget *parent = 0);
	~Viewport();

	void prepareLines();
	void reset(int nbins, multi_img::Value binsize, multi_img::Value minval);
	void setLimiters(int label);

	int dimensionality;
	bool gradient;
	std::vector<BinSet> sets;
	std::vector<QString> labels;
	QGLBuffer vb;
	std::vector<std::pair<int, QByteArray> > shuffleIdx;

	const std::vector<multi_img::Value> *illuminant;
	bool illuminant_correction;

	int selection, hover;
	bool limiterMode;
	std::vector<std::pair<int, int> > limiters;
	bool active, wasActive;

	float useralpha;

	bool showLabeled, showUnlabeled, ignoreLabels;
	bool overlayMode;
	QPolygonF overlayPoints;

public slots:
	void killHover();
	void toggleRGB(bool enabled)
	{ drawRGB = enabled; update(); }
	void activate();

	// entry and exit point of user interaction with quick drawing
	void startNoHQ(bool resize = false);
	void endNoHQ();

	// acknowledge folding
	void folding() { drawingState = FOLDING; resizeTimer.start(50); }

	void screenshot();

signals:
	void bandSelected(int dim, bool gradient);
	void newOverlay(int dim);
	void activated();
	void addSelection();
	void remSelection();

protected:
	void paintEvent(QPaintEvent*);
	void resizeEvent(QResizeEvent*);
	void enterEvent(QEvent*);
	void mouseMoveEvent(QMouseEvent*);
	void mousePressEvent(QMouseEvent*);
	void mouseReleaseEvent(QMouseEvent*);
	void wheelEvent(QWheelEvent *);
	void keyPressEvent(QKeyEvent *);

	// helper function that updates Y-axis labels
	void updateYAxis();

	// helper function that updates world transformation
	void updateModelview();

	// helper functions called by mouseMoveEvent
	void updateXY(int sel, int bin);

	// helper functions called by paintEvent
	void drawBins(QPainter&);
	void drawAxesBg(QPainter&);
	void drawAxesFg(QPainter&);
	void drawLegend(QPainter&);
	void drawRegular();
	void drawOverlay();

	// helper for limiter handling
	bool updateLimiter(int dim, int bin);

	// cached information about image
	int nbins;
	multi_img::Value binsize, minval, maxval;

private:
	// modelview matrix and its inverse
	QTransform modelview, modelviewI;
	// zoom and shift in y-direction
	qreal zoom;
	qreal shift;
	int lasty;

	/* if in limiter mode, user has to release mouse button before switching
	   band. this is for usability, users tend to accidentially switch bands */
	bool holdSelection;
	int *activeLimiter;

	// cache for efficient overlay
	bool cacheValid;
	QImage cacheImg;

	// draw without highlight
	bool clearView;
	bool implicitClearView;

	// drawing mode mean vs. bin center
	bool drawMeans;
	// drawing mode labelcolor vs. sRGB color
	bool drawRGB;
	// draw with antialiasing
	bool drawHQ;

	enum {
		HIGH_QUALITY,        // drawing HQ as usual
		HIGH_QUALITY_QUICK,  // last update was HQ, quick updates requested
		QUICK,               // last update not HQ, quick updates requested
		RESIZE,              // resize updates requested (last update invalid)
		SCREENSHOT,          // screenshot update requested (special drawing)
		FOLDING              // only draw blank during folding resize ops
	} drawingState;
	// this timer will re-enable regular drawing after resize/folding
	QTimer resizeTimer;

	std::vector<QString> yaxis;
	int yaxisWidth;
};

#endif // VIEWPORT_H
