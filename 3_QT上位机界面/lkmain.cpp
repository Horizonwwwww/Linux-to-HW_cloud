#include "lkmain.h"
#include "ui_lkmain.h"
#include "lklogin.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QFile>  // 引入QFile类


lkmain::lkmain(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::lkmain)


{
    ui->setupUi(this);

    // 调用样式设置函数
    applyStyles();

    // 定时刷新（在构造函数中添加）
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &lkmain::fetchCloudData);
    timer->start(5000); // 每5秒刷新一次

    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, &QNetworkAccessManager::finished, this, &lkmain::onReplyFinished);

    getAuthToken();  // <<== 初始化时先获取Token

    // 可选：设置窗口属性
    setWindowTitle("主界面");
}

lkmain::~lkmain()
{
    delete ui;
}

//HWCLOUD
//---------------------------------------------------------------------------------------
// HW--------------------------------------------------------------------------------------
void lkmain::getAuthToken()
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
void lkmain::fetchCloudData()
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
void lkmain::onReplyFinished(QNetworkReply *reply)
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
        //ui->labelTemperature->setText(QString::number(properties["Temperature"].toDouble()));
        //ui->labelHumidity->setText(QString::number(properties["Humidity"].toDouble()));
        ui->lineEdit_weight->setText(QString::number(properties["weight"].toDouble(), 'f', 2) + " kg");
        // 获取 weight 的值，乘以 0.5，再设置到 lineEdit_money
        double weight = properties["weight"].toDouble();  // 获取 weight 的值
        double money = weight * 0.5;                      // 计算 0.5 倍
        ui->lineEdit_money->setText(QString::number(money, 'f', 2) + " 元");  // 设置到 lineEdit_money

        // 其他数据类似...
    }

    reply->deleteLater();
}


//------------------------------------------------------------------------------------------------------------------------


// 定义样式设置函数--------------------------------------------
void lkmain::applyStyles() {


        ui->frame->setStyleSheet(
                    "QFrame {"
                    "   background-image: url(:/images/xqj.jpg);"  // 图片路径（使用资源系统）
                    "}"
                    );
        ui->frame_2->setStyleSheet(
                    "QFrame {"
                    "   background-image: url(:/images/xqj.jpg);"  // 图片路径（使用资源系统）
                    "}"
                    );

    // 其他控件样式...
}

//------------------------------------------------------------------------------------------

void lkmain::on_pushButton_3_clicked()
{
    lklogin *loginWin = new lklogin();
    loginWin->setAttribute(Qt::WA_DeleteOnClose);
    loginWin->show();
    this->close();


}

void lkmain::on_refresh_clicked()
{
     fetchCloudData();
}
