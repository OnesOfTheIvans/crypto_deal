#include "graphical/StatusPresentation.hpp"

#include <QApplication>
#include <QLabel>
#include <QString>
#include <gtest/gtest.h>

namespace {
    QApplication &getApplication()
    {
        auto *existingApplication = qobject_cast<QApplication *>(QApplication::instance());
        if (existingApplication != nullptr)
        {
            return *existingApplication;
        }

        static int argumentCount = 1;
        static char applicationName[] = "StatusPresentationTests";
        static char *arguments[]{applicationName, nullptr};
        static QApplication application(argumentCount, arguments);
        return application;
    }
}

TEST(StatusPresentationTest, AppliesSemanticPropertyAndAccessibleDescription)
{
    getApplication();
    QLabel label;

    applyStatusPresentation(label, StatusPresentation::NEUTRAL);
    EXPECT_EQ(label.property("statusPresentation").toString(), QString("neutral"));
    EXPECT_EQ(label.accessibleDescription(), QString("Informational status"));

    applyStatusPresentation(label, StatusPresentation::LOADING);
    EXPECT_EQ(label.property("statusPresentation").toString(), QString("loading"));
    EXPECT_EQ(label.accessibleDescription(), QString("Loading status"));

    applyStatusPresentation(label, StatusPresentation::SUCCESS);
    EXPECT_EQ(label.property("statusPresentation").toString(), QString("success"));
    EXPECT_EQ(label.accessibleDescription(), QString("Successful status"));

    applyStatusPresentation(label, StatusPresentation::WARNING);
    EXPECT_EQ(label.property("statusPresentation").toString(), QString("warning"));
    EXPECT_EQ(label.accessibleDescription(), QString("Warning status"));

    applyStatusPresentation(label, StatusPresentation::ERROR);
    EXPECT_EQ(label.property("statusPresentation").toString(), QString("error"));
    EXPECT_EQ(label.accessibleDescription(), QString("Error status"));
}
