#include <QProgressDialog>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>

#include "dltexporter.h"
#include "fieldnames.h"
#include "project.h"
#include "optmanager.h"

#include <cmath>

namespace {
//-----------------------------------------------------------------------------
class ErrorReporter
{
public:
    ErrorReporter(QWidget* parent): parent(parent)
    {

    }

    void report(const QString& text)
    {
        QMessageBox::critical(parent, "DLT Viewer", text);
    }

private:
    QWidget* parent;
};

//-----------------------------------------------------------------------------
struct ExportContext
{
    QFile* to;
    QDltFile* from;
    QDltPluginManager* pluginManager;
    QModelIndexList* selection;
    QList<int> selectedRows;
    DltExporter::DltExportSelection exportSelection;
    std::unique_ptr<ErrorReporter> reporter;
    bool silent_mode;
};

//-----------------------------------------------------------------------------
class SelectionHelper
{
public:
    //-------------------------------------------------------------------------
    static int size(ExportContext& ctx)
    {
        /* calculate size */
        switch(ctx.exportSelection) {
        case DltExporter::SelectionAll:
            return ctx.from->size();
            break;

        case DltExporter::SelectionFiltered:
            return ctx.from->sizeFilter();
            break;

        case DltExporter::SelectionSelected:
            return ctx.selectedRows.size();
            break;

        default:
            return -1;
        }
    }

    //-------------------------------------------------------------------------
    static int selection(ExportContext& context, int num)
    {
        switch (context.exportSelection) {
        case DltExporter::SelectionAll:
            return num;

        case DltExporter::SelectionFiltered:
            return context.from->getMsgFilterPos(num);

        case DltExporter::SelectionSelected:
            return context.from->getMsgFilterPos(context.selectedRows.at(num));

        default:
            return -1;
        }
    }

    //-------------------------------------------------------------------------
    static QByteArray getMsg(ExportContext& ctx, int num)
    {
        if(ctx.exportSelection == DltExporter::SelectionAll)
            return ctx.from->getMsg(num);
        else if(ctx.exportSelection == DltExporter::SelectionFiltered)
            return ctx.from->getMsgFilter(num);
        else if(ctx.exportSelection == DltExporter::SelectionSelected)
            return ctx.from->getMsgFilter(ctx.selectedRows[num]);
        else
            return QByteArray();
    }

    //-------------------------------------------------------------------------
    static void prepare(ExportContext& ctx)
    {
        if(ctx.exportSelection == DltExporter::SelectionSelected && ctx.selection != NULL)
        {
            std::sort(ctx.selection->begin(), ctx.selection->end());
            ctx.selectedRows.clear();

            for (const QModelIndex& index: *ctx.selection) {
                ctx.selectedRows.append(index.row());
            }
        }
    }
};

//-----------------------------------------------------------------------------
class FormatHelper
{
public:
    virtual bool open() = 0;
    virtual bool prepare() = 0;
    virtual bool readMsg(int num, QDltMsg &msg, QByteArray &buf) = 0;
    virtual void decodeMsg(QDltMsg& msg, QByteArray& buf) = 0;
    virtual bool exportMsg(int num, QDltMsg &msg, QByteArray &buf) = 0;
    virtual bool finish() = 0;
    virtual ~FormatHelper() = default;
};

//-----------------------------------------------------------------------------
class BaseFormatHelper: public FormatHelper
{
public:
    //-------------------------------------------------------------------------
    BaseFormatHelper(std::shared_ptr<ExportContext> ctx): ctx(ctx)
    {

    }

    //-------------------------------------------------------------------------
    bool prepare() override
    {
        return true;
    }

    //-------------------------------------------------------------------------
    bool readMsg(int num, QDltMsg &msg, QByteArray &buf) override
    {
        buf.clear();
        buf = SelectionHelper::getMsg(*ctx, num);
        if(buf.isEmpty())
            return false;
        return msg.setMsg(buf);
    }

    //-------------------------------------------------------------------------
    void decodeMsg(QDltMsg& msg, QByteArray& buf) override
    {
        ctx->pluginManager->decodeMsg(msg, ctx->silent_mode);
    }

    //-------------------------------------------------------------------------
    bool finish() override
    {
        if (ctx->to) {
            ctx->to->close();
        }
        return true;
    }

protected:
    std::shared_ptr<ExportContext> ctx;
};

//-----------------------------------------------------------------------------
class DltFormatHelper: public BaseFormatHelper
{
public:
    using BaseFormatHelper::BaseFormatHelper;

    //-------------------------------------------------------------------------
    bool open() override
    {
        if(!ctx->to->open(QIODevice::WriteOnly))
        {
            ctx->reporter->report("Cannot open the export file.");
            return false;
        }

        return true;
    }

    //-------------------------------------------------------------------------
    void decodeMsg(QDltMsg &msg, QByteArray &buf) override
    {

    }

    //-------------------------------------------------------------------------
    bool exportMsg(int num, QDltMsg &msg, QByteArray &buf) override
    {
        ctx->to->write(buf);
        return true;
    }
};

//-----------------------------------------------------------------------------
class DltDecodedFormatHelper: public DltFormatHelper
{
public:
    using DltFormatHelper::DltFormatHelper;

    //-------------------------------------------------------------------------
    void decodeMsg(QDltMsg &msg, QByteArray &buf) override
    {
        BaseFormatHelper::decodeMsg(msg, buf);
        msg.setNumberOfArguments(msg.sizeArguments());
        msg.getMsg(buf, true);
    }
};

//-----------------------------------------------------------------------------
class BaseTextFormatHelper: public BaseFormatHelper
{
public:
    using BaseFormatHelper::BaseFormatHelper;

    //-------------------------------------------------------------------------
    bool open() override
    {
        if(!ctx->to->open(QIODevice::WriteOnly | QIODevice::Text))
        {
            ctx->reporter->report("Cannot open the export file.");
            return false;
        }

        return true;
    }
};

//-----------------------------------------------------------------------------
class PlainTextFormatHelper: public BaseTextFormatHelper
{
public:
    using BaseTextFormatHelper::BaseTextFormatHelper;

    //-------------------------------------------------------------------------
    bool exportMsg(int num, QDltMsg &msg, QByteArray&) override
    {
        QString text;

        /* get message ASCII text */
        auto index = SelectionHelper::selection(*ctx, num);
        if (index < 0) {
            return false;
        } else {
            text += QString("%1 ").arg(index);
        }

        text += msg.toStringHeader();
        text += " ";
        text += msg.toStringPayload().simplified();
        text += "\n";
        try
        {
            writeMessage(text);
        }
        catch (...)
        {
        }

        return true;
    }

    virtual void writeMessage(QString text) = 0;
};

//-----------------------------------------------------------------------------
class ClipboardFormatHelper: public PlainTextFormatHelper
{
public:
    using PlainTextFormatHelper::PlainTextFormatHelper;

    //-------------------------------------------------------------------------
    bool open() override
    {

    }

    //-------------------------------------------------------------------------
    bool finish() override
    {
        QApplication::clipboard()->setText(text);
        return true;
    }

    //-------------------------------------------------------------------------
    virtual void writeMessage(QString message) override
    {
        PlainTextFormatHelper::finish();
        text += message;
    }

private:
    QString text;
};

//-----------------------------------------------------------------------------
class ASCIIFormatHelper: public PlainTextFormatHelper
{
public:
    using PlainTextFormatHelper::PlainTextFormatHelper;

    //-------------------------------------------------------------------------
    void writeMessage(QString text) override
    {
        ctx->to->write(text.toLatin1().constData());
    }
};

//-----------------------------------------------------------------------------
class UTF8FormatHelper: public PlainTextFormatHelper
{
public:
    using PlainTextFormatHelper::PlainTextFormatHelper;

    //-------------------------------------------------------------------------
    void writeMessage(QString text) override
    {
        ctx->to->write(text.toUtf8().constData());
    }
};

//-----------------------------------------------------------------------------
class CSVFormatHelper: public BaseTextFormatHelper
{
public:
    using BaseTextFormatHelper::BaseTextFormatHelper;

    //-------------------------------------------------------------------------
    bool prepare() override
    {
        /* Write the first line of CSV file */
        if(!writeCSVHeader())
        {
            ctx->reporter->report("Cannot write to export file.");
            return false;
        }
        return true;
    }

    //-------------------------------------------------------------------------
    bool exportMsg(int num, QDltMsg &msg, QByteArray &buf) override
    {
        auto index = SelectionHelper::selection(*ctx, num);
        if (index < 0) {
            return false;
        } else {
            writeCSVLine(num, msg);
        }
    }

private:
    //-------------------------------------------------------------------------
    bool writeCSVHeader()
    {
        QString header("\"%1\",\"%2\",\"%3\",\"%4\",\"%5\",\"%6\",\"%7\",\"%8\",\"%9\",\"%10\",\"%11\",\"%12\",\"%13\"\n");
        header = header.arg(FieldNames::getName(FieldNames::Index))
                        .arg(FieldNames::getName(FieldNames::Time))
                        .arg(FieldNames::getName(FieldNames::TimeStamp))
                        .arg(FieldNames::getName(FieldNames::Counter))
                        .arg(FieldNames::getName(FieldNames::EcuId))
                        .arg(FieldNames::getName(FieldNames::AppId))
                        .arg(FieldNames::getName(FieldNames::ContextId))
                        .arg(FieldNames::getName(FieldNames::SessionId))
                        .arg(FieldNames::getName(FieldNames::Type))
                        .arg(FieldNames::getName(FieldNames::Subtype))
                        .arg(FieldNames::getName(FieldNames::Mode))
                        .arg(FieldNames::getName(FieldNames::ArgCount))
                        .arg(FieldNames::getName(FieldNames::Payload));
        return (ctx->to->write(header.toLatin1().constData())) < 0 ? false : true;
    }

    //-------------------------------------------------------------------------
    void writeCSVLine(int index, QDltMsg msg)
    {
        QString text("");

        text += escapeCSVValue(QString("%1").arg(index)).append(",");
        text += escapeCSVValue(QString("%1.%2").arg(msg.getTimeString()).arg(msg.getMicroseconds(),6,10,QLatin1Char('0'))).append(",");
        text += escapeCSVValue(QString("%1.%2").arg(msg.getTimestamp()/10000).arg(msg.getTimestamp()%10000,4,10,QLatin1Char('0'))).append(",");
        text += escapeCSVValue(QString("%1").arg(msg.getMessageCounter())).append(",");
        text += escapeCSVValue(QString("%1").arg(msg.getEcuid())).append(",");
        text += escapeCSVValue(QString("%1").arg(msg.getApid())).append(",");
        text += escapeCSVValue(QString("%1").arg(msg.getCtid())).append(",");
        text += escapeCSVValue(QString("%1").arg(msg.getSessionid())).append(",");
        text += escapeCSVValue(QString("%1").arg(msg.getTypeString())).append(",");
        text += escapeCSVValue(QString("%1").arg(msg.getSubtypeString())).append(",");
        text += escapeCSVValue(QString("%1").arg(msg.getModeString())).append(",");
        text += escapeCSVValue(QString("%1").arg(msg.getNumberOfArguments())).append(",");
        text += escapeCSVValue(msg.toStringPayload().simplified());
        text += "\n";

        ctx->to->write(text.toLatin1().constData());
    }

    //-------------------------------------------------------------------------
    QString escapeCSVValue(QString arg)
    {
        QString retval = arg.replace(QChar('\"'), QString("\"\""));
        retval = QString("\"%1\"").arg(retval);
        return retval;
    }

};

}





namespace {

//-----------------------------------------------------------------------------
class ProgressHandler
{
public:
    virtual void start(int size) = 0;
    virtual void update(int count) = 0;
    virtual void done() = 0;
    virtual ~ProgressHandler() = default;
};

//-----------------------------------------------------------------------------
class DialogProgressHandler: public ProgressHandler
{
public:
    //-------------------------------------------------------------------------
    DialogProgressHandler(QObject* parent)
        : _parent(qobject_cast<QWidget*>(parent))
        , _dialog(nullptr)
        , _size(0)
    {
    }

    //-------------------------------------------------------------------------
    void start(int size) override
    {
        _size = size;
        _last = 0;
        /* init fileprogress */
        _dialog.reset(new QProgressDialog(
                    "Export ...",
                    "Cancel",
                    0,
                    100,
                    _parent
        ));

        _dialog->setWindowTitle("DLT Viewer");
        _dialog->setWindowModality(Qt::WindowModal);
        _dialog->show();
    }

    //-------------------------------------------------------------------------
    void update(int count) override
    {
        if (!_dialog) {
            return;
        }

        int progress = std::round(double(count) / _size * 100.0);
        if (progress > _last) {
            _dialog->setValue(progress);
        }
    }

    //-------------------------------------------------------------------------
    void done() override
    {
        if (_dialog) {
            _dialog->close();
        }
    }

private:
    QWidget* _parent;
    std::unique_ptr<QProgressDialog> _dialog;
    int _size;
    int _last;
};

//-----------------------------------------------------------------------------
class SilentProgressHandler: public ProgressHandler
{
public:
    void start(int) {}
    void update(int) {}
    void done() {}
};

//-----------------------------------------------------------------------------
class DltExporterImpl
{
public:
    //-------------------------------------------------------------------------
    DltExporterImpl(DltExporter::DltExportFormat exportFormat, std::shared_ptr<ExportContext> ctx)
        : helper(nullptr)
        , ctx(ctx)
    {
        switch(exportFormat) {
        case DltExporter::FormatAscii:
            helper.reset(new ASCIIFormatHelper(ctx));
            break;

        case DltExporter::FormatClipboard:
            helper.reset(new ClipboardFormatHelper(ctx));
            break;

        case DltExporter::FormatCsv:
            helper.reset(new CSVFormatHelper(ctx));
            break;

        case DltExporter::FormatDlt:
            helper.reset(new DltFormatHelper(ctx));
            break;

        case DltExporter::FormatDltDecoded:
            helper.reset(new DltDecodedFormatHelper(ctx));
            break;

        case DltExporter::FormatUTF8:
            helper.reset(new UTF8FormatHelper(ctx));
        }
    }

    //-------------------------------------------------------------------------
    bool start()
    {
        SelectionHelper::prepare(*ctx);

        if (!helper->open()) {
            return false;
        }

        if (!helper->prepare()) {
            return false;
        }

        /* success */
        return true;
    }

    //-------------------------------------------------------------------------
    void exportMessages(QWidget* parent)
    {
        QDltMsg msg;
        QByteArray buf;

        /* initialise values */
        int readErrors = 0;
        int exportErrors = 0;
        int exportCounter = 0;
        int startFinishError = 0;

        std::unique_ptr<ProgressHandler> progress;
        if (this->ctx->silent_mode) {
            progress.reset(new DialogProgressHandler(parent));
        } else {
            progress.reset(new SilentProgressHandler());
        }

        /* start export */
        if(!this->start())
        {
            qDebug() << "DLT Export start() failed";
            startFinishError++;
            return;
        }

        int size = SelectionHelper::size(*ctx);

        qDebug() << "Start DLT export of"
                 << size
                 << "messages"
                 << "silent mode"
                 << !this->ctx->silent_mode;

        progress->start(size);

        for(int num = 0; num < size ; num++)
        {
            progress->update(num);

            // get message
            if(!helper->readMsg(num, msg, buf))
            {
                qDebug() << "DLT Export getMsg() failed on msg " << num;
                readErrors++;
                continue;
            }

            // decode message if needed
            helper->decodeMsg(msg, buf);

            // export message
            if(!helper->exportMsg(num, msg, buf))
            {
              qDebug() << "DLT Export exportMsg() failed";
              exportErrors++;
              continue;
            } else {
                exportCounter++;
            }

        } // for loop

        progress->done();

        if (!helper->finish())
        {
            startFinishError++;
        }

        if (startFinishError > 0 || readErrors > 0 || exportErrors > 0)
        {
           qDebug() << "DLT Export finish() failed";
           QMessageBox::warning(
                       NULL,
                       "Export Errors!",
                       QString("Exported successful: %1 / %2\n\nReadErrors:%3\nWriteErrors:%4\nStart/Finish errors:%5")
                        .arg(exportCounter)
                        .arg(size)
                        .arg(readErrors)
                        .arg(exportErrors)
                        .arg(startFinishError));
           return;
        }
        qDebug() << "DLT export done for" << exportCounter << "messages with result" << startFinishError;
    }

private:
    std::unique_ptr<FormatHelper> helper;
    std::shared_ptr<ExportContext> ctx;
};

}

//-----------------------------------------------------------------------------
// DltExporter
//-----------------------------------------------------------------------------
DltExporter::DltExporter(QObject *parent) :
    QObject(parent)
{

}

//-----------------------------------------------------------------------------
void DltExporter::exportMessages(
        QDltFile *from,
        QFile *to,
        QDltPluginManager *pluginManager,
        DltExporter::DltExportFormat exportFormat,
        DltExporter::DltExportSelection exportSelection,
        QModelIndexList *selection)
{
    std::shared_ptr<ExportContext> ctx =
            std::make_shared<ExportContext>();

    auto parent_widget = qobject_cast<QWidget*>(parent());
    ctx->reporter.reset(new ErrorReporter(parent_widget));
    ctx->to = to;
    ctx->from = from;
    ctx->pluginManager = pluginManager;
    ctx->selection = selection;
    ctx->exportSelection = exportSelection;
    // Just what the bloody hell is that?
    ctx->silent_mode = !OptManager::getInstance()->issilentMode();

    DltExporterImpl impl(exportFormat, ctx);

    impl.exportMessages(parent_widget);
}


