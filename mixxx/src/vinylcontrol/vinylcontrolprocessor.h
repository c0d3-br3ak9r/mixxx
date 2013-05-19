#ifndef VINYLCONTROLPROCESSOR_H
#define VINYLCONTROLPROCESSOR_H

#include <QObject>
#include <QThread>
#include <QVector>
#include <QMutex>
#include <QWaitCondition>
#include <QReadWriteLock>

#include "configobject.h"
#include "util/fifo.h"
#include "vinylcontrol/vinylsignalquality.h"
#include "soundmanagerutil.h"

class VinylControl;
class ControlPushButton;

class VinylControlProcessor : public QThread, public AudioDestination {
    Q_OBJECT
  public:
    VinylControlProcessor(QObject* pParent, ConfigObject<ConfigValue>* pConfig);
    virtual ~VinylControlProcessor();

    // Called from main thread. Must only touch m_bReportSignalQuality.
    void setSignalQualityReporting(bool enable);

    // Called from the main thread. Must only touch m_bQuit;
    void shutdown();

    // Called from the main thread. Must only touch m_bReload;
    void requestReloadConfig();

    FIFO<VinylSignalQualityReport>* getSignalQualityFifo() {
        return &m_signalQualityFifo;
    }

  public slots:
    virtual void onInputConnected(AudioInput input);
    virtual void onInputDisconnected(AudioInput input);

    // Called by the engine callback. Must not touch any state in
    // VinylControlProcessor except for m_samplePipes.
    void receiveBuffer(AudioInput input, const short* pBuffer,
                       unsigned int iNumFrames);

  protected:
    void run();

  private slots:
    void toggleDeck(double value);

  private:
    void reloadConfig();

    ConfigObject<ConfigValue>* m_pConfig;
    ControlPushButton* m_pToggle;
    // A pre-allocated array of FIFOs for writing samples from the engine
    // callback to the processor thread. There is a maximum of
    // kMaximumVinylControlInputs pipes.
    FIFO<short>* m_samplePipes[kMaximumVinylControlInputs];
    short* m_pWorkBuffer;
    QWaitCondition m_samplesAvailableSignal;
    QMutex m_waitForSampleMutex;
    QReadWriteLock m_processorsLock;
    QVector<VinylControl*> m_processors;
    FIFO<VinylSignalQualityReport> m_signalQualityFifo;
    volatile bool m_bReportSignalQuality;
    volatile bool m_bQuit;
    volatile bool m_bReloadConfig;
};


#endif /* VINYLCONTROLPROCESSOR_H */
