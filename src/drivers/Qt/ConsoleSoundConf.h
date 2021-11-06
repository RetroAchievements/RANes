// GameSoundConf.h
//

#ifndef __GameSndH__
#define __GameSndH__

#include <QWidget>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QFrame>
#include <QGroupBox>
#include <QProgressBar>
#include <QTimer>

class ConsoleSndConfDialog_t : public QDialog
{
	Q_OBJECT

public:
	ConsoleSndConfDialog_t(QWidget *parent = 0);
	~ConsoleSndConfDialog_t(void);

protected:
	void closeEvent(QCloseEvent *event);

	int  sndQuality;
	QCheckBox *enaChkbox;
	QCheckBox *enaLowPass;
	QCheckBox *swapDutyChkbox;
	QCheckBox *useGlobalFocus;
	QComboBox *qualitySelect;
	QComboBox *rateSelect;
	QSlider *bufSizeSlider;
	QLabel *bufSizeLabel;
	QLabel *volLbl;
	QLabel *triLbl;
	QLabel *sqr1Lbl;
	QLabel *sqr2Lbl;
	QLabel *nseLbl;
	QLabel *pcmLbl;
	QLabel *starveLbl;
	QSlider *sqr2Slider;
	QSlider *nseSlider;
	QSlider *pcmSlider;
	QProgressBar *bufUsage;
	QTimer       *updateTimer;

	void setCheckBoxFromProperty(QCheckBox *cbx, const char *property);
	void setComboBoxFromProperty(QComboBox *cbx, const char *property);
	void setSliderFromProperty(QSlider *slider, QLabel *lbl, const char *property);
	void setSliderEnables(void);

private slots:
	void closeWindow(void);
	void resetCounters(void);
	void periodicUpdate(void);
	void bufSizeChanged(int value);
	void volumeChanged(int value);
	void triangleChanged(int value);
	void square1Changed(int value);
	void square2Changed(int value);
	void noiseChanged(int value);
	void pcmChanged(int value);
	void enaSoundStateChange(int value);
	void enaSoundLowPassChange(int value);
	void swapDutyCallback(int value);
	void useGlobalFocusChanged(int value);
	void soundQualityChanged(int index);
	void soundRateChanged(int index);
};

#endif
