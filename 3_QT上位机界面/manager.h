#ifndef MANAGER_H
#define MANAGER_H

#include <QWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>

namespace Ui {
class manager;
}

class manager : public QWidget
{
    Q_OBJECT

public:
    explicit manager(QWidget *parent = nullptr);
    ~manager();

    void getAuthToken();
    void fetchCloudData();


private slots:
    void on_pushButton_2_clicked();

    void on_pushButton_clicked();

    void onReplyFinished(QNetworkReply *reply);

    void on_btnRefresh_clicked();

    void updateSensorDisplay();
    void startReadingSensorData();      // 启动定时器读取数据

private:
    Ui::manager *ui;
    QNetworkAccessManager *networkManager;
    QTimer *timer;
    QString authToken;  // 存储 IAM Token
    void applyStyles();  // 添加这一行声明

};

#endif // MANAGER_H
