#ifndef STATUS_PRESENTATION_H
#define STATUS_PRESENTATION_H

class QLabel;

enum class StatusPresentation
{
    NEUTRAL,
    LOADING,
    SUCCESS,
    WARNING,
    ERROR
};

void applyStatusPresentation(QLabel &label, StatusPresentation presentation);

#endif
