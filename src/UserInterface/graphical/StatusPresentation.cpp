#include "StatusPresentation.hpp"

#include <QLabel>
#include <QString>
#include <QStyle>

namespace {
    QString getStatusProperty(StatusPresentation presentation)
    {
        switch (presentation)
        {
        case StatusPresentation::NEUTRAL:
            return "neutral";
        case StatusPresentation::LOADING:
            return "loading";
        case StatusPresentation::SUCCESS:
            return "success";
        case StatusPresentation::WARNING:
            return "warning";
        case StatusPresentation::ERROR:
            return "error";
        }

        return "neutral";
    }

    QString getAccessibleStatusDescription(StatusPresentation presentation)
    {
        switch (presentation)
        {
        case StatusPresentation::NEUTRAL:
            return "Informational status";
        case StatusPresentation::LOADING:
            return "Loading status";
        case StatusPresentation::SUCCESS:
            return "Successful status";
        case StatusPresentation::WARNING:
            return "Warning status";
        case StatusPresentation::ERROR:
            return "Error status";
        }

        return "Informational status";
    }
}

void applyStatusPresentation(QLabel &label, StatusPresentation presentation)
{
    label.setProperty("statusPresentation", getStatusProperty(presentation));
    label.setAccessibleDescription(getAccessibleStatusDescription(presentation));
    label.style()->unpolish(&label);
    label.style()->polish(&label);
    label.update();
}
