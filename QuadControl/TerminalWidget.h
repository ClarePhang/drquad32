#ifndef TERMINALWIDGET_H
#define TERMINALWIDGET_H

#include <QWidget>
#include <QString>
#include <QPlainTextEdit>
#include <QTextCharFormat>
#include <QTextCursor>


class TerminalWidget : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget *parent = 0);
    ~TerminalWidget();

    QString read(size_t maxSize = SIZE_MAX);
    void write(const QString &text);

protected:
    struct Attributes {
        int foreground = 7;
        int background = 0;
        bool bold = false;
        bool underline = false;
        bool blink = false;
        bool reverse = false;
    };

    struct Modes {
        bool overwrite = false;
        bool echo = false;
    };

    Attributes  attributes;
    Modes       modes;
    QTextCursor cursor;

    int         rxState = 0;
    QString     rxBuffer;
    QString     txBuffer;

    virtual bool event(QEvent *e) override;
    virtual void paintEvent(QPaintEvent *e) override;

    void printText(const QString &text);
    void parseSgr(const QList<int> &params);
    void parseModes(const QList<int> &params, bool set);
    void parseCursor(const QList<int> &params, char command);
    void parseMulti(const QString &seq);
    void parseSingle(const QChar seq);
    void parseChar(char c);
};

#endif // TERMINALWIDGET_H
