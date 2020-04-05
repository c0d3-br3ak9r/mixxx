#include <QtDebug>

#include "effects/effectknobparameterslot.h"

#include "effects/effectslot.h"
#include "effects/effectparameter.h"
#include "control/controleffectknob.h"
#include "control/controlobject.h"
#include "control/controlpushbutton.h"
#include "controllers/softtakeover.h"
#include "util/xml.h"

EffectKnobParameterSlot::EffectKnobParameterSlot(const QString& group, const unsigned int iParameterSlotNumber)
        : EffectParameterSlotBase(group, iParameterSlotNumber, EffectParameterType::KNOB) {
    QString itemPrefix = formatItemPrefix(iParameterSlotNumber);

    m_pControlValue = new ControlEffectKnob(
            ConfigKey(m_group, itemPrefix));
    connect(m_pControlValue, &ControlObject::valueChanged,
            this, &EffectKnobParameterSlot::slotValueChanged);

    m_pControlLoaded = new ControlObject(
            ConfigKey(m_group, itemPrefix + QString("_loaded")));
    m_pControlLoaded->setReadOnly();

    m_pControlType = new ControlObject(
            ConfigKey(m_group, itemPrefix + QString("_type")));
    m_pControlType->setReadOnly();

    m_pControlLinkType = new ControlPushButton(
            ConfigKey(m_group, itemPrefix + QString("_link_type")));
    m_pControlLinkType->setButtonMode(ControlPushButton::TOGGLE);
    m_pControlLinkType->setStates(
        static_cast<double>(EffectManifestParameter::LinkType::NUM_LINK_TYPES));
    m_pControlLinkType->connectValueChangeRequest(
            this, &EffectKnobParameterSlot::slotLinkTypeChanging);

    m_pControlLinkInverse = new ControlPushButton(
            ConfigKey(m_group, itemPrefix + QString("_link_inverse")));
    m_pControlLinkInverse->setButtonMode(ControlPushButton::TOGGLE);
    connect(m_pControlLinkInverse, &ControlObject::valueChanged,
            this, &EffectKnobParameterSlot::slotLinkInverseChanged);

    m_pMetaknobSoftTakeover = new SoftTakeover();

    clear();
}

EffectKnobParameterSlot::~EffectKnobParameterSlot() {
    // qDebug() << debugString() << "destroyed";
    delete m_pControlValue;
    // m_pControlLoaded and m_pControlType are deleted by ~EffectParameterSlotBase
    delete m_pControlLinkType;
    delete m_pControlLinkInverse;
    delete m_pMetaknobSoftTakeover;
}

void EffectKnobParameterSlot::loadParameter(EffectParameterPointer pEffectParameter) {
    // qDebug() << debugString() << "loadParameter" << (pEffectSlot ? pEffectSlot->getManifest().name() : "(null)");
    clear();

    VERIFY_OR_DEBUG_ASSERT(pEffectParameter->manifest()->parameterType() ==
            EffectParameterType::KNOB) {
        return;
    }

    m_pEffectParameter = pEffectParameter;

    if (m_pEffectParameter) {
        m_pManifestParameter = m_pEffectParameter->manifest();

        //qDebug() << debugString() << "Loading effect parameter" << m_pEffectParameter->manifest()->id();
        double dValue = m_pEffectParameter->getValue();
        double dMinimum = m_pManifestParameter->getMinimum();
        double dMinimumLimit = dMinimum; // TODO(rryan) expose limit from EffectParameter
        double dMaximum = m_pManifestParameter->getMaximum();
        double dMaximumLimit = dMaximum; // TODO(rryan) expose limit from EffectParameter
        double dDefault = m_pManifestParameter->getDefault();

        // qDebug() << debugString()
        //          << QString("Val: %1 Min: %2 MinLimit: %3 Max: %4 MaxLimit: %5 Default: %6")
        //          .arg(dValue).arg(dMinimum).arg(dMinimumLimit).arg(dMaximum).arg(dMaximumLimit).arg(dDefault);

        EffectManifestParameter::ValueScaler type = m_pManifestParameter->valueScaler();
        m_pControlValue->setBehaviour(type, dMinimum, dMaximum);
        m_pControlValue->setDefaultValue(dDefault);
        m_pControlValue->set(dValue);
        // TODO(rryan) expose this from EffectParameter
        m_pControlType->forceSet(static_cast<double>(type));
        // Default loaded parameters to loaded and unlinked
        m_pControlLoaded->forceSet(1.0);

        m_pControlLinkType->set(
            static_cast<double>(m_pManifestParameter->defaultLinkType()));
        m_pControlLinkInverse->set(
            static_cast<double>(m_pManifestParameter->defaultLinkInversion()));
    }

    emit(updated());
}

void EffectKnobParameterSlot::clear() {
    //qDebug() << debugString() << "clear";
    if (m_pEffectParameter) {
        m_pEffectParameter = nullptr;
        m_pManifestParameter.clear();
    }

    m_pControlLoaded->forceSet(0.0);
    m_pControlValue->set(0.0);
    m_pControlValue->setDefaultValue(0.0);
    m_pControlType->forceSet(0.0);
    m_pControlLinkType->setAndConfirm(
        static_cast<double>(EffectManifestParameter::LinkType::NONE));
    m_pMetaknobSoftTakeover->setThreshold(SoftTakeover::kDefaultTakeoverThreshold);
    m_pControlLinkInverse->set(0.0);
    emit(updated());
}

void EffectKnobParameterSlot::slotParameterValueChanged(double value) {
    //qDebug() << debugString() << "slotParameterValueChanged" << value.toDouble();
    m_pControlValue->set(value);
}

void EffectKnobParameterSlot::slotLinkTypeChanging(double v) {
    m_pMetaknobSoftTakeover->ignoreNext();
    EffectManifestParameter::LinkType newType =
        static_cast<EffectManifestParameter::LinkType>(
            static_cast<int>(v));
    if (newType == EffectManifestParameter::LinkType::LINKED_LEFT ||
        newType == EffectManifestParameter::LinkType::LINKED_RIGHT ||
        newType == EffectManifestParameter::LinkType::LINKED_LEFT_RIGHT) {
        double neutral = m_pManifestParameter->neutralPointOnScale();
        if (neutral > 0.0 && neutral < 1.0) {
            // Knob is already a split knob, meaning it has a positive and
            // negative effect if it's twisted above the neutral point or
            // below the neutral point.
            // Toggle back to 0
            newType = EffectManifestParameter::LinkType::NONE;
        }
    }
    if (newType == EffectManifestParameter::LinkType::LINKED_LEFT ||
        newType == EffectManifestParameter::LinkType::LINKED_RIGHT) {
        m_pMetaknobSoftTakeover->setThreshold(
                SoftTakeover::kDefaultTakeoverThreshold * 2.0);
    } else {
        m_pMetaknobSoftTakeover->setThreshold(SoftTakeover::kDefaultTakeoverThreshold);
    }
    m_pControlLinkType->setAndConfirm(static_cast<double>(newType));
    m_pEffectParameter->setLinkType(newType);
}

void EffectKnobParameterSlot::slotLinkInverseChanged(double v) {
    Q_UNUSED(v);
    m_pMetaknobSoftTakeover->ignoreNext();
    m_pEffectParameter->setLinkInversion(
            static_cast<EffectManifestParameter::LinkInversion>(
                    static_cast<int>(v)));
}

void EffectKnobParameterSlot::onEffectMetaParameterChanged(double parameter, bool force) {
    m_dChainParameter = parameter;
    if (m_pEffectParameter != nullptr) {
        // Intermediate cast to integer is needed for VC++.
        EffectManifestParameter::LinkType type =
                static_cast<EffectManifestParameter::LinkType>(
                        static_cast<int>(m_pControlLinkType->get()));

        bool inverse = m_pControlLinkInverse->toBool();
        double neutral = m_pManifestParameter->neutralPointOnScale();

        switch (type) {
            case EffectManifestParameter::LinkType::LINKED:
                if (parameter < 0.0 || parameter > 1.0) {
                    return;
                }
                if (neutral > 0.0 && neutral < 1.0) {
                    if (inverse) {
                        // the neutral position must stick where it is
                        neutral = 1.0 - neutral;
                    }
                    // Knob is already a split knob
                    // Match to center position of meta knob
                    if (parameter <= 0.5) {
                        parameter /= 0.5;
                        parameter *= neutral;
                    } else {
                        parameter -= 0.5;
                        parameter /= 0.5;
                        parameter *= 1 - neutral;
                        parameter += neutral;
                    }
                }
                break;
            case EffectManifestParameter::LinkType::LINKED_LEFT:
                if (parameter >= 0.5 && parameter <= 1.0) {
                    parameter = 1;
                } else if (parameter >= 0.0 && parameter <= 0.5) {
                    parameter *= 2;
                } else {
                    return;
                }
                break;
            case EffectManifestParameter::LinkType::LINKED_RIGHT:
                if (parameter >= 0.5 && parameter <= 1.0) {
                    parameter -= 0.5;
                    parameter *= 2;
                } else if (parameter >= 0.0 && parameter < 0.5) {
                    parameter = 0.0;
                } else {
                    return;
                }
                break;
            case EffectManifestParameter::LinkType::LINKED_LEFT_RIGHT:
                if (parameter >= 0.5 && parameter <= 1.0) {
                    parameter -= 0.5;
                    parameter *= 2;
                } else if (parameter >= 0.0 && parameter < 0.5) {
                    parameter *= 2;
                    parameter = 1.0 - parameter;
                } else {
                    return;
                }
                break;
            case EffectManifestParameter::LinkType::NONE:
            default:
                return;
        }

        if (inverse) {
            parameter = 1.0 - parameter;
        }

        //qDebug() << "onEffectMetaParameterChanged" << debugString() << parameter << "force?" << force;
        if (force) {
            m_pControlValue->setParameterFrom(parameter, NULL);
            // This ensures that softtakover is in sync for following updates
            m_pMetaknobSoftTakeover->ignore(m_pControlValue, parameter);
        } else if (!m_pMetaknobSoftTakeover->ignore(m_pControlValue, parameter)) {
            m_pControlValue->setParameterFrom(parameter, NULL);
        }
    }
}

void EffectKnobParameterSlot::syncSofttakeover() {
    double parameter = m_pControlValue->getParameter();
    m_pMetaknobSoftTakeover->ignore(m_pControlValue, parameter);
}

double EffectKnobParameterSlot::getValueParameter() const {
    return m_pControlValue->getParameter();
}
