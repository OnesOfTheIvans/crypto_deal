#include "DecimalInputField.hpp"

#include "graphical/GuiLayoutConstants.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

using namespace GuiLayoutConstants;

DecimalInputField::DecimalInputField(const QString &namePrefix, const QString &inputObjectName, QWidget *parent)
    : QWidget(parent), input(nullptr), validationError(nullptr), correctionPanel(nullptr),
      correctionDescription(nullptr), useCorrectionButton(nullptr), dismissCorrectionButton(nullptr),
      focusInteractionStarted(false), validationFeedbackEnabled(false)
{
    setObjectName(namePrefix + "Field");
    createLayout(namePrefix, inputObjectName);
}

QLineEdit &DecimalInputField::getInput() const
{
    return *input;
}

bool DecimalInputField::hasValidationFeedbackEnabled() const
{
    return validationFeedbackEnabled;
}

void DecimalInputField::setInputEnabled(bool enabled)
{
    input->setEnabled(enabled);
}

void DecimalInputField::showValidation(const QString &error, const std::optional<QString> &proposedCorrection)
{
    validationError->setText(error);
    const bool hasVisibleError = validationFeedbackEnabled && !error.isEmpty();
    validationError->setVisible(hasVisibleError);
    applyValidationStyle(hasVisibleError);

    const bool hasVisibleCorrection = validationFeedbackEnabled && proposedCorrection.has_value() &&
                                      !proposedCorrection->isEmpty() && dismissedInput != input->text();
    if (hasVisibleCorrection)
    {
        correction = proposedCorrection.value();
        correctionDescription->setText("Suggested valid increment: " + correction);
        useCorrectionButton->setText("Use " + correction);
    }
    else
    {
        correction.clear();
    }
    correctionPanel->setVisible(hasVisibleCorrection);
}

void DecimalInputField::clearValidation()
{
    validationError->clear();
    validationError->hide();
    correction.clear();
    dismissedInput.clear();
    correctionPanel->hide();
    applyValidationStyle(false);
}

void DecimalInputField::createLayout(const QString &namePrefix, const QString &inputObjectName)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ORDER_FORM_FEEDBACK_SPACING);

    input = new QLineEdit(this);
    input->setObjectName(inputObjectName);
    input->setProperty("orderInput", true);
    input->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);
    input->installEventFilter(this);

    validationError = new QLabel(this);
    validationError->setObjectName(namePrefix + "Error");
    validationError->setProperty("validationError", true);
    validationError->setTextFormat(Qt::PlainText);
    validationError->setWordWrap(true);
    validationError->hide();

    correctionPanel = new QWidget(this);
    correctionPanel->setObjectName(namePrefix + "Correction");
    correctionPanel->setProperty("correctionPanel", true);
    auto *correctionLayout = new QHBoxLayout(correctionPanel);
    correctionLayout->setContentsMargins(0, 0, 0, 0);
    correctionLayout->setSpacing(ORDER_FORM_FEEDBACK_SPACING);

    correctionDescription = new QLabel(correctionPanel);
    correctionDescription->setProperty("correctionDescription", true);
    correctionDescription->setWordWrap(true);

    useCorrectionButton = new QPushButton(correctionPanel);
    useCorrectionButton->setObjectName(namePrefix + "UseCorrection");
    useCorrectionButton->setProperty("secondaryOrderAction", true);

    dismissCorrectionButton = new QPushButton("Dismiss", correctionPanel);
    dismissCorrectionButton->setObjectName(namePrefix + "DismissCorrection");
    dismissCorrectionButton->setProperty("secondaryOrderAction", true);

    correctionLayout->addWidget(correctionDescription, 1);
    correctionLayout->addWidget(useCorrectionButton);
    correctionLayout->addWidget(dismissCorrectionButton);
    correctionPanel->hide();

    layout->addWidget(input);
    layout->addWidget(validationError);
    layout->addWidget(correctionPanel);

    connect(input,
            &QLineEdit::textChanged,
            this,
            [this](const QString &text)
            {
                if (text != dismissedInput)
                {
                    dismissedInput.clear();
                }
                emit inputChanged(text);
            });
    connect(useCorrectionButton,
            &QPushButton::clicked,
            this,
            [this]()
            {
                if (!correction.isEmpty())
                {
                    input->setText(correction);
                }
            });
    connect(dismissCorrectionButton,
            &QPushButton::clicked,
            this,
            [this]()
            {
                dismissedInput = input->text();
                correction.clear();
                correctionPanel->hide();
            });
}

bool DecimalInputField::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == input && event->type() == QEvent::FocusIn)
    {
        focusInteractionStarted = true;
    }
    else if (watched == input && event->type() == QEvent::FocusOut && focusInteractionStarted &&
             !validationFeedbackEnabled)
    {
        validationFeedbackEnabled = true;
        emit requestValidation();
    }

    return QWidget::eventFilter(watched, event);
}

void DecimalInputField::applyValidationStyle(bool hasError)
{
    input->setProperty("validationError", hasError);
    input->style()->unpolish(input);
    input->style()->polish(input);
}
