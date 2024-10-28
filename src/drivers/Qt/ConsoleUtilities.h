// ConsoleUtilities.h

#pragma once

#include <string>

#include <QColor>
#include <QTimer>
#include <QValidator>
#include <QDialog>
#include <QHelpEvent>
#include <QCheckBox>

int  getDirFromFile( const char *path, std::string &dir );

const char *getRomFile( void );

int getFileBaseName( const char *filepath, char *base, char *suffix = nullptr );

int parseFilepath( const char *filepath, std::string *dir, std::string *base = nullptr, std::string *suffix = nullptr );

const char *fceuExecutablePath(void);

int fceuLoadConfigColor( const char *confName, QColor *color );

class fceuDecIntValidtor : public QValidator
{ 
   public:
   	fceuDecIntValidtor( long long int min, long long int max, QObject *parent);

		QValidator::State validate(QString &input, int &pos) const;

		void  setMinMax( long long int min, long long int max );
	private:
		long long int  min;
		long long int  max;
};

class fceuHexIntValidtor : public QValidator
{ 
	public:
		fceuHexIntValidtor( long long int min, long long int max, QObject *parent);

		QValidator::State validate(QString &input, int &pos) const;

		void  setMinMax( long long int min, long long int max );
	private:
		long long int  min;
		long long int  max;
};

class fceuCustomToolTip : public QDialog
{
	Q_OBJECT

	public:
		fceuCustomToolTip( QWidget *parent = nullptr );
		~fceuCustomToolTip( void );

		void setHideOnMouseMove(bool);
	protected:
		bool eventFilter(QObject *obj, QEvent *event) override;
		void mouseMoveEvent( QMouseEvent *event ) override;

		void hideTip(void);
		void hideTipImmediately(void);
	private:
		QWidget *w;
		QTimer  *hideTimer;
		bool     hideOnMouseMode;

		static fceuCustomToolTip *instance;

	private slots:
		void  hideTimerExpired(void);
};

// Read Only Checkbox for state display only.
class QCheckBoxRO : public QCheckBox
{
	Q_OBJECT

	public:
		QCheckBoxRO( const QString &text, QWidget *parent = nullptr );
		QCheckBoxRO( QWidget *parent = nullptr );

	protected:
		void mousePressEvent( QMouseEvent *event ) override;
		void mouseReleaseEvent( QMouseEvent *event ) override;

};

QString fceuGetOpcodeToolTip( uint8_t *opcode, int size );

QDialog *fceuCustomToolTipShow( const QPoint &globalPos, QDialog *popup );
