#include <QApplication>
#include "main_window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ServoErrorAnalyzer");
    app.setApplicationVersion("1.0");

    MainWindow w;
    w.show();

    return app.exec();
}
