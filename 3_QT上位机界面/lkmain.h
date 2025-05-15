#ifndef LKMAIN_H
#define LKMAIN_H

#include <QWidget>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QTimer>
#include <QJsonArray>
#include "lklogin.h"
#include <QMainWindow>


class lklogin;  // 告诉编译器这是一个类

namespace Ui {
class lkmain;
}

class lkmain : public QWidget
{
    Q_OBJECT

public:
    explicit lkmain(QWidget *parent = nullptr);
    ~lkmain();

    void getAuthToken();
    void fetchCloudData();

private slots:

    void on_pushButton_3_clicked();
    void onReplyFinished(QNetworkReply *reply);

    void on_refresh_clicked();

private:
    Ui::lkmain *ui;

    void applyStyles();  // 添加这一行声明

    QNetworkAccessManager *networkManager;
    QTimer *timer;
    QString authToken;  // 存储 IAM Token

};

#endif // LKMAIN_H
