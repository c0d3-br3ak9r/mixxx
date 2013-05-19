#include <QReadLocker>
#include <QWriteLocker>

#include "vinylcontrol/vinylcontrolprocessor.h"

#include "vinylcontrol/defs_vinylcontrol.h"
#include "vinylcontrol/vinylcontrol.h"
#include "vinylcontrol/vinylcontrolxwax.h"
#include "controlpushbutton.h"
#include "defs.h"
#include "util/timer.h"

#define SIGNAL_QUALITY_FIFO_SIZE 256
#define SAMPLE_PIPE_FIFO_SIZE 65536

VinylControlProcessor::VinylControlProcessor(QObject* pParent, ConfigObject<ConfigValue> *pConfig)
        : QThread(pParent),
          m_pConfig(pConfig),
          m_pToggle(new ControlPushButton(ConfigKey(VINYL_PREF_KEY, "Toggle"))),
          m_pWorkBuffer(new short[MAX_BUFFER_LEN]),
          m_processors(kMaximumVinylControlInputs, NULL),
          m_signalQualityFifo(SIGNAL_QUALITY_FIFO_SIZE),
          m_bReportSignalQuality(false),
          m_bQuit(false),
          m_bReloadConfig(false) {
    connect(m_pToggle, SIGNAL(valueChanged(double)),
            this, SLOT(toggleDeck(double)),
            Qt::DirectConnection);

    for (int i = 0; i < kMaximumVinylControlInputs; ++i) {
        //
        m_samplePipes[i] = new FIFO<short>(SAMPLE_PIPE_FIFO_SIZE);
    }

    start();
}

VinylControlProcessor::~VinylControlProcessor() {
    m_bQuit = true;
    m_samplesAvailableSignal.wakeAll();
    wait();

    delete m_pToggle;
    delete [] m_pWorkBuffer;

    {
        QWriteLocker locker(&m_processorsLock);
        for (int i = 0; i < kMaximumVinylControlInputs; ++i) {
            VinylControl* pProcessor = m_processors.at(i);
            delete pProcessor;
            m_processors[i] = NULL;

            delete m_samplePipes[i];
            m_samplePipes[i] = NULL;
        }
    }

    // xwax has a global LUT that we need to free after we've shut down our
    // vinyl control threads because it's not thread-safe.
    VinylControlXwax::freeLUTs();
}

void VinylControlProcessor::setSignalQualityReporting(bool enable) {
    m_bReportSignalQuality = enable;
}

void VinylControlProcessor::shutdown() {
    m_bQuit = true;
    m_samplesAvailableSignal.wakeAll();
}

void VinylControlProcessor::requestReloadConfig() {
    m_bReloadConfig = true;
    m_samplesAvailableSignal.wakeAll();
}

void VinylControlProcessor::run() {
    unsigned static id = 0; //the id of this thread, for debugging purposes //XXX copypasta (should factor this out somehow), -kousu 2/2009
    QThread::currentThread()->setObjectName(QString("VinylControlProcessor %1").arg(++id));

    while (true) {
        if (m_bQuit) {
            break;
        }

        if (m_bReloadConfig) {
            reloadConfig();
            m_bReloadConfig = false;
        }

        for (int i = 0; i < kMaximumVinylControlInputs; ++i) {
            QReadLocker locker(&m_processorsLock);
            VinylControl* pProcessor = m_processors[i];
            locker.unlock();
            FIFO<short>* pSamplePipe = m_samplePipes[i];

            if (pSamplePipe->readAvailable() > 0) {
                int samplesRead = pSamplePipe->read(m_pWorkBuffer, MAX_BUFFER_LEN);

                if (pProcessor) {
                    pProcessor->analyseSamples(m_pWorkBuffer, samplesRead);
                } else {
                    // Samples are being written to a non-existent processor. Warning?
                    qWarning() << "Samples written to non-existent VinylControl processor:" << i;
                }
            }

            // TODO(rryan) define a time-based update rate. This will update way
            // too quickly.
            if (pProcessor && m_bReportSignalQuality) {
                VinylSignalQualityReport report;
                if (pProcessor->writeQualityReport(&report)) {
                    report.processor = i;
                    if (m_signalQualityFifo.write(&report, 1) != 1) {
                        qWarning() << "VinylControlProcessor could not write signal quality report for VC index:" << i;
                    }
                }
            }
        }

        if (m_bQuit) {
            break;
        }

        // Wait for a signal from the main thread or engine thread that we
        // should wake up and process input.
        m_waitForSampleMutex.lock();
        m_samplesAvailableSignal.wait(&m_waitForSampleMutex);
        m_waitForSampleMutex.unlock();
    }
}

void VinylControlProcessor::reloadConfig() {
    QWriteLocker locker(&m_processorsLock);

    for (int i = 0; i < kMaximumVinylControlInputs; ++i) {
        VinylControl* pCurrent = m_processors[i];

        if (pCurrent == NULL) {
            continue;
        }

        delete pCurrent;
        VinylControl *pNew = new VinylControlXwax(
            m_pConfig, kVCGroup.arg(i + 1));
        m_processors.replace(i, pNew);
    }
}

void VinylControlProcessor::onInputConnected(AudioInput input) {
    if (input.getType() != AudioInput::VINYLCONTROL) {
        qDebug() << "WARNING: AudioInput type is not VINYLCONTROL. Ignoring.";
        return;
    }
    unsigned char index = input.getIndex();

    if (index >= kMaximumVinylControlInputs) {
        // Should not be possible.
        qWarning() << "VinylControlProcessor::onInputConnected got invalid index:" << index;
        return;
    }

    VinylControl *pNew = new VinylControlXwax(
        m_pConfig, kVCGroup.arg(index + 1));

    QWriteLocker locker(&m_processorsLock);
    VinylControl* pCurrent = m_processors.at(index);
    m_processors.replace(index, pNew);
    delete pCurrent;
}

void VinylControlProcessor::onInputDisconnected(AudioInput input) {
    if (input.getType() != AudioInput::VINYLCONTROL) {
        qDebug() << "WARNING: AudioInput type is not VINYLCONTROL. Ignoring.";
        return;
    }

    unsigned char index = input.getIndex();

    if (index >= kMaximumVinylControlInputs) {
        // Should not be possible.
        qWarning() << "VinylControlProcessor::onInputDisconnected got invalid index:" << index;
        return;
    }

    QWriteLocker locker(&m_processorsLock);
    VinylControl* pVC = m_processors.at(index);
    m_processors.replace(index, NULL);
    delete pVC;
}


void VinylControlProcessor::receiveBuffer(AudioInput input,
                                          const short *pBuffer,
                                          unsigned int nFrames) {
    ScopedTimer t("VinylControlProcessor::receiveBuffer");
    if (input.getType() != AudioInput::VINYLCONTROL) {
        qDebug() << "WARNING: AudioInput type is not VINYLCONTROL. Ignoring incoming buffer.";
        return;
    }


    unsigned char vcIndex = input.getIndex();

    if (vcIndex >= kMaximumVinylControlInputs) {
        // Should not be possible.
        return;
    }

    FIFO<short>* pSamplePipe = m_samplePipes[vcIndex];

    if (pSamplePipe == NULL) {
        // Should not be possible.
        return;
    }

    const int nSamples = nFrames * 2;
    int samplesWritten = pSamplePipe->write(pBuffer, nSamples);

    if (samplesWritten < nSamples) {
        qWarning() << "ERROR: Buffer overflow in VinylControlProcessor. Dropping samples on the floor."
                   << "VCIndex:" << vcIndex;
    }

    m_samplesAvailableSignal.wakeAll();
}

void VinylControlProcessor::toggleDeck(double value) {
    if (!value)
        return;

    /** few different cases here:
     * 1. No decks have vinyl control enabled.
     * 2. One deck has vinyl control enabled.
     * 3. Many decks have vinyl control enabled.
     *
     * For case 1, we'll just enable vinyl control on the first deck. Case 2
     * is the most common one, we'll just turn off the vinyl control on the
     * deck currently using it and turn it on on the next one (sequentially,
     * wrapping as needed). Behaviour in case 3 is totally non-obvious and
     * will be ignored.
     */

    // -1 means we haven't found a proxy that's enabled
    int enabled = -1;

    QReadLocker locker(&m_processorsLock);

    for (int i = 0; i < m_processors.size(); ++i) {
        VinylControl* pProcessor = m_processors.at(i);
        if (pProcessor && pProcessor->isEnabled()) {
            if (enabled > -1) {
                return; // case 3
            }
            enabled = i;
        }
    }

    if (enabled > -1 && m_processors.size() > 1) {
        // handle case 2

        int nextProxy = (enabled + 1) % m_processors.size();
        while (!m_processors[nextProxy]) {
            nextProxy = (nextProxy + 1) % m_processors.size();
        } // guaranteed to terminate as there's at least 1 non-null proxy

        if (nextProxy == enabled) {
            return;
        }

        m_processors[enabled]->toggleVinylControl(false);
        m_processors[nextProxy]->toggleVinylControl(true);
    } else if (enabled == -1) {
        // handle case 1, or we just don't have any processors
        foreach (VinylControl* pProcessor, m_processors) {
            if (pProcessor) {
                pProcessor->toggleVinylControl(true);
                break;
            }
        }
    }
}
