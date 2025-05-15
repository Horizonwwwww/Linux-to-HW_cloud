#include "lklogin.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    lklogin w;
    w.show();

    return a.exec();
}
