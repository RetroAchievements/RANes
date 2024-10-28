// GameViewerSDL.h
//

#pragma  once

#include <QWidget>
#include <QColor>
#include <QCursor>
#include <QPaintEvent>
#include <QResizeEvent>
#include <SDL.h>

#include "Qt/ConsoleViewerInterface.h"

class ConsoleViewSDL_t : public QWidget, public ConsoleViewerBase
{
    Q_OBJECT

	public:
		ConsoleViewSDL_t(QWidget *parent = 0);
		~ConsoleViewSDL_t(void);

		int  init(void);
		void reset(void);
		void cleanup(void);
		void render(void);
		void queueRedraw(void){ render(); };
		int  driver(void){ return VIDEO_DRIVER_SDL; };

		void transfer2LocalBuffer(void);

		void setVsyncEnable( bool ena );
		void setLinearFilterEnable( bool ena );

		bool   getForceAspectOpt(void){ return forceAspect; };
		void   setForceAspectOpt( bool val ){ forceAspect = val; return; };
		bool   getAutoScaleOpt(void){ return autoScaleEna; };
		void   setAutoScaleOpt( bool val ){ autoScaleEna = val; return; };
		double getScaleX(void){ return xscale; };
		double getScaleY(void){ return yscale; };
		void   setScaleXY( double xs, double ys );
		void   getNormalizedCursorPos( double &x, double &y );
		bool   getMouseButtonState( unsigned int btn );
		void   setAspectXY( double x, double y );
		void   getAspectXY( double &x, double &y );
		double getAspectRatio(void);

		void   setCursor(const QCursor &c);
		void   setCursor( Qt::CursorShape s );
		void   setBgColor( QColor &c );

		QSize   size(void){ return QWidget::size(); };
		QCursor cursor(void){ return QWidget::cursor(); };
		void    setMinimumSize(const QSize &s){ return QWidget::setMinimumSize(s); };
		void    setMaximumSize(const QSize &s){ return QWidget::setMaximumSize(s); };

	protected:

	//void paintEvent(QPaintEvent *event);
	void showEvent(QShowEvent *event);
	void resizeEvent(QResizeEvent *event);
	void mousePressEvent(QMouseEvent * event);
	void mouseReleaseEvent(QMouseEvent * event);

	int  view_width;
	int  view_height;

	double devPixRatio;
	double aspectRatio;
	double aspectX;
	double aspectY;
	double xscale;
	double yscale;
	int  rw;
	int  rh;
	int  sx;
	int  sy;
	int  sdlRendW;
	int  sdlRendH;

	bool vsyncEnabled;
	bool linearFilter;
	bool forceAspect;
	bool autoScaleEna;
	QColor *bgColor;

	uint32_t  *localBuf;
	uint32_t   localBufSize;
	unsigned int mouseButtonMask;

 	SDL_Window   *sdlWindow;
	SDL_Renderer *sdlRenderer;
	SDL_Texture  *sdlTexture;
	SDL_Cursor   *sdlCursor;
	//SDL_Rect      sdlViewport;

	private slots:
};

