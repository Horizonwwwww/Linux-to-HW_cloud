#include "manager.h"
#include "ui_manager.h"
#include "lklogin.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QDebug>

manager::manager(QWidget *parent) :
    QWidget(parent),

    ui(new Ui::manager)
{
    ui->setupUi(this);

    applyStyles();

    startReadingSensorData();  // 启动文件读取

    // 定时刷新（在构造函数中添加）
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &manager::fetchCloudData);
    timer->start(5000); // 每5秒刷新一次

    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, &QNetworkAccessManager::finished, this, &manager::onReplyFinished);

    getAuthToken();  // <<== 初始化时先获取Token
}

manager::~manager()
{
    delete ui;
}

void manager::on_pushButton_2_clicked()
{
    close();
}

void manager::on_pushButton_clicked()
{
    lklogin *loginWin = new lklogin();
    loginWin->setAttribute(Qt::WA_DeleteOnClose);
    loginWin->show();
    this->close();
}

// HW--------------------------------------------------------------------------------------
void manager::getAuthToken()
{
    QUrl url("https://iam.cn-north-4.myhuaweicloud.com/v3/auth/tokens");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject domainObj;
    domainObj.insert("name", "hw021564820");  // 企业账号名称（主账号）

    QJsonObject userObj;
    userObj.insert("name", "LK_IAM");  // IAM 用户名
    userObj.insert("password", "LKLK_0511");       // IAM 密码
    userObj.insert("domain", domainObj);

    QJsonObject passwordObj;
    passwordObj.insert("user", userObj);

    QJsonObject identityObj;
    identityObj.insert("methods", QJsonArray() << "password");
    identityObj.insert("password", passwordObj);

    QJsonObject scopeObj;
    scopeObj.insert("project", QJsonObject{{"id", "3ecac23f93a747989f921021022f1d4f"}});  // 项目ID

    QJsonObject authObj;
    authObj.insert("identity", identityObj);
    authObj.insert("scope", scopeObj);

    QJsonObject rootObj;
    rootObj.insert("auth", authObj);

    QJsonDocument doc(rootObj);
    QByteArray body = doc.toJson();

    qDebug() << "Sending auth request with body:" << body;
    QNetworkReply *reply = networkManager->post(request, body);

    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "Error:" << reply->errorString() << "Response:" << reply->readAll();
            reply->deleteLater();
            return;
        }

        // 从响应头中获取 Token
        //QVariant token = reply->rawHeader("X-Subject-Token");
        //authToken = token.toString();  // 保存 Token 字符串，供后续请求使用

        QString token = reply->rawHeader("X-Subject-Token");
        if (token.isEmpty()) {
            qDebug() << "Failed to get token. Headers:";
            for (const QByteArray &header : reply->rawHeaderList()) {
                qDebug() << header << ":" << reply->rawHeader(header);
            }
            reply->deleteLater();
            return;
        }
        authToken = token;


        qDebug() << "Auth Token:" << authToken;

        //调试
        qDebug() << "Response status code:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "All headers:";
        const QList<QByteArray> headers = reply->rawHeaderList();
        for (const QByteArray &header : headers) {
            qDebug() << header << ":" << reply->rawHeader(header);
        }


        reply->deleteLater();

        // 获取成功后立即拉取云端数据
        fetchCloudData();
    });
}
//添加函数发送GET请求到华为云接口：
void manager::fetchCloudData()
{
    if (authToken.isEmpty()) {
        qDebug() << "Token is empty, request skipped.";
        return;
    }

    QUrl apiUrl("https://82be2f428c.st1.iotda-app.cn-north-4.myhuaweicloud.com:443/v5/iot/3ecac23f93a747989f921021022f1d4f/devices/67d3ded4375e694aa6927974_test_linux/shadow");
    QNetworkRequest request(apiUrl);

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("X-Auth-Token", authToken.toUtf8());  // 使用 IAM Token

    networkManager->get(request);
}

//实现槽函数解析JSON并更新UI：
void manager::onReplyFinished(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "Error:" << reply->errorString();
        reply->deleteLater();
        return;
    }

    QByteArray response = reply->readAll();
    QJsonDocument jsonDoc = QJsonDocument::fromJson(response);
    QJsonObject root = jsonDoc.object();

    //处理SSL错误（如需要）：
    connect(reply, &QNetworkReply::sslErrors, [=](const QList<QSslError> &errors) {
        for (const QSslError &error : errors) {
            qDebug() << "SSL Error:" << error.errorString();
        }
    });

    // 解析设备影子数据
    QJsonArray shadowArray = root["shadow"].toArray();
    if (!shadowArray.isEmpty()) {
        QJsonObject shadow = shadowArray[0].toObject();
        QJsonObject reported = shadow["reported"].toObject();
        QJsonObject properties = reported["properties"].toObject();

        // 更新UI控件
        ui->labelTemperature->setText(QString::number(properties["Temperature"].toDouble()) + " ℃");
        ui->labelHumidity->setText(QString::number(properties["Humidity"].toDouble()) + " %");
        ui->labelWeight->setText(QString::number(properties["weight"].toDouble()) + " kg");
        ui->labelMQ->setText(properties["MQ_2"].toString());

        ui->labelTemperature->adjustSize();
        ui->labelHumidity->adjustSize();
        ui->labelWeight->adjustSize();
        // 其他数据类似...
    }

    reply->deleteLater();
}


// HW--------------------------------------------------------------------------------------

// SensorDATA--------------------------------------------------------------------------------------
void manager::startReadingSensorData()
{
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &manager::updateSensorDisplay);
    timer->start(1000); // 每秒读取一次
}

void manager::updateSensorDisplay()
{
    QFile file("/tmp/sensor_data.txt");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&file);
    QString line = in.readLine();
    QStringList values = line.split(" ");

    if (values.size() >= 3) {
        float temp = values[0].toFloat();
        float hum = values[1].toFloat();
        float weight = values[2].toFloat();
        float mq2_voltage = values[3].toFloat();
        float led_status = values[4].toFloat();

        ui->labelTemperature_2->setText(QString("温度: %1 °C").arg(temp));
        ui->labelHumidity_2->setText(QString("湿度: %1 %").arg(hum));
        ui->labelWeight_2->setText(QString("重量: %1 kg").arg(weight));
        ui->labelMQ_2->setText(QString("MQ电压值: %1 V").arg(mq2_voltage));
        ui->label_LED->setText(QString("LED: %1 ").arg(led_status));
    }

    file.close();
}



// SensorDATA--------------------------------------------------------------------------------------


// 定义样式设置函数--------------------------------------------
void manager::applyStyles()
{
        ui->frame->setStyleSheet
                (
                    "QFrame {"
                    "   background-image: url(:/images/go.jpg);"  // 图片路径（使用资源系统）

                    "}"
                    );
    // 其他控件样式...
}

//------------------------------------------------------------------------------------------



// 按钮点击槽函数
void manager::on_btnRefresh_clicked()
{
    fetchCloudData();
}
