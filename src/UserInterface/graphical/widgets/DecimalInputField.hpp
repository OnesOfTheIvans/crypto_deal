#ifndef DECIMAL_INPUT_FIELD_H
#define DECIMAL_INPUT_FIELD_H

#include <QString>
#include <QWidget>

#include <optional>

class QEvent;
class QLabel;
class QLineEdit;
class QPushButton;

class DecimalInputField final : public QWidget
{
    Q_OBJECT

  private:
    QLineEdit *input;
    QLabel *validationError;
    QWidget *correctionPanel;
    QLabel *correctionDescription;
    QPushButton *useCorrectionButton;
    QPushButton *dismissCorrectionButton;
    QString correction;
    QString dismissedInput;
    bool focusInteractionStarted;
    bool validationFeedbackEnabled;

    void createLayout(const QString &namePrefix, const QString &inputObjectName);

    void applyValidationStyle(bool hasError);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  public:
    DecimalInputField(const QString &namePrefix, const QString &inputObjectName, QWidget *parent = nullptr);

    QLineEdit &getInput() const;

    bool hasValidationFeedbackEnabled() const;

    void setInputEnabled(bool enabled);

    void showValidation(const QString &error, const std::optional<QString> &proposedCorrection = std::nullopt);

    void clearValidation();

  signals:
    void inputChanged(const QString &text);

    void requestValidation();
};

#endif
