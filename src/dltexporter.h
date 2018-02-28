#ifndef DLTEXPORTER_H
#define DLTEXPORTER_H

#include <QObject>
#include <QFile>
#include <QModelIndexList>
#include <QTreeWidget>

#include <memory>

#include "qdlt.h"

class DltExporter : public QObject
{
    Q_OBJECT

public:

    typedef enum { FormatDlt,FormatAscii,FormatCsv,FormatClipboard,FormatDltDecoded,FormatUTF8} DltExportFormat;

    typedef enum { SelectionAll,SelectionFiltered,SelectionSelected } DltExportSelection;

public:

    /* Default QT constructor.
     * Please pass a window as a parameter to parent dialogs correctly.
     */
    explicit DltExporter(QObject *parent = 0);

    /* Export some messages from QDltFile to a CSV file.
     * \param from QDltFile to pull messages from
     * \param to Regular file to export to
     * \param pluginManager The treewidget representing plugins. Needed to run decoders.
     * \param exportFormat
     * \param exportSelection
     * \param selection Limit export to these messages. Leave to NULL to export everything,
     */
    void exportMessages(QDltFile *from, QFile *to, QDltPluginManager *pluginManager,
                             DltExporter::DltExportFormat exportFormat, DltExporter::DltExportSelection exportSelection, QModelIndexList *selection = 0);


signals:
    
public slots:
};

#endif // DLTEXPORTER_H
