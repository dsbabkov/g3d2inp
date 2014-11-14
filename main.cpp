#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QSettings>
#include <QMessageBox>
//#ifdef QT_DEBUG
    #include <QDebug>
//#endif

struct Node
{
    inline bool operator == (const Node& b)
    {
        return this->ix == b.ix && this->iy == b.iy && this->iz == b.iz;
    }

    inline bool operator != (const Node& b)
    {
        return !(*this == b);
    }

    QString x, y, z;
    float ix, iy, iz;
    bool original;
    int ref;
};


struct Elem
{
    int vol;
    int nodes[4];
};

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    a.setOrganizationName("CTT");
    a.setApplicationName("g3d2inp");

    QSettings settings;

    QString srcFile = settings.value("src").toString();
    QString destFile = settings.value("dest").toString();

#ifndef QT_DEBUG
    srcFile = QFileDialog::getOpenFileName(0, "Open", srcFile, "G3D (*.g3d)");
    if (srcFile.isEmpty())
        return -1;
    settings.setValue("src", srcFile);

    destFile = QFileDialog::getSaveFileName(0, "Save", destFile, "Abaqus (*.inp)");
    if (destFile.isEmpty())
        return -1;
    settings.setValue("dest", destFile);
#endif
    QFile src(srcFile), dest(destFile);

    if(!src.open(QFile::ReadOnly | QFile::Text) || !dest.open(QFile::WriteOnly | QFile::Text))
    {
        QString msg = QString("Unable to open %1 file").arg(src.isOpen() ? "dest" : "src");
        QMessageBox::critical(0, "Error", msg);
        return -1;
    }

    //Чтение g3d

    QVector<Node> nodes[2];
    QVector<Elem> elems[2];

    for (int type = 0; type < 2; ++type)
    {
        QVector<Node>& curNodes = nodes[type];
        QVector<Elem>& curElems = elems[type];

        QStringList splitedLine = QString(src.readLine().trimmed()).split(' ', QString::SkipEmptyParts);
        curNodes.resize(splitedLine.first().toInt());
        curElems.resize(splitedLine.at(1).toInt());

        for(int i = 0; i < curNodes.size(); ++i)
        {
            Node& node = curNodes[i];
            splitedLine = QString(src.readLine().trimmed()).split(' ', QString::SkipEmptyParts);
            node.ix = (node.x = splitedLine.first()).toFloat() * 1000;
            node.iy = (node.y = splitedLine.at(1)).toFloat() * 1000;
            node.iz = (node.z = splitedLine.at(2)).toFloat() * 1000;
        }

        for(int i = 0; i < curElems.size(); ++i)
        {
            splitedLine = QString(src.readLine().trimmed()).split(' ', QString::SkipEmptyParts);
            curElems[i].vol = splitedLine.first().toInt();
            for (int j = 1; j < 5; ++j)
                curElems[i].nodes[j - 1] = splitedLine.at(j).toInt();
        }

        for(int i = 0; i < curElems.size(); ++i)
            src.readLine();
    }

    QString tmpLine;
    do
    {
        tmpLine = src.readLine();
    } while (tmpLine.left(7) != "#VOLUME" || src.atEnd());

    int nVolumes = src.readLine().trimmed().toInt();
    QStringList volumeNames;
    for (int i = 0; i < nVolumes; ++i)
        volumeNames << src.readLine().trimmed();

    src.close();

    //Вычисление совпадающих узлов
    Node* nodes0 = nodes[0].data(),
        * nodes1 = nodes[1].data(),
        * nodes0End = nodes0 + nodes[0].size(),
        * nodes1End = nodes1 + nodes[1].size();

    int ref = 1;
    for (Node* n1 = nodes0; n1 != nodes0End; ++n1)
    {
        for (Node* n2 = nodes1; n2 != nodes1End; ++n2)
        {
            if (*n2 == *n1)
            {
//                n2->original = false;
                n2->original = true;
                n2->ref = ref;
            }
            else
                n2->original = true;
        }
        ++ref;
    }

    // Запись inp
    dest.write("*NODE\n");

    int addNode = 1;
    for (int type = 0; type < 2; ++type)
    {
        QVector<Node>& curNodes = nodes[type];

        for (int i = 0; i < curNodes.size(); ++i)
        {
            Node& node = curNodes[i];
            dest.write(QString("%1, %2, %3, %4\n")
                              .arg(i + addNode)
                              .arg(node.ix)
                              .arg(node.iy)
                              .arg(node.iz).toStdString().data());
        }
        addNode += curNodes.size();
    }

    addNode = 0;
    int addElem = 1;
    int curVolume = -1;

    //элементы отливки

    QVector<Elem>& elem0 = elems[0];

    for (int i = 0; i < elem0.size(); ++i)
    {
        const Elem elem = elem0.at(i);
        if(curVolume != elem.vol)
        {
            curVolume = elem.vol;
            dest.write(QString("*ELEMENT,TYPE=C3D4,ELSET=Casting-%1\n").arg(curVolume).toStdString().data());
        }
        dest.write(QString("%1, %2, %3, %4, %5\n")
                   .arg(i + 1)
                   .arg(elem.nodes[0] + addNode)
                .arg(elem.nodes[1] + addNode)
                .arg(elem.nodes[2] + addNode)
                .arg(elem.nodes[3] + addNode).toStdString().data());
    }

    addNode = nodes[0].size();
    addElem += elem0.size();
    curVolume = -1;

    QVector<Elem>& elem1 = elems[1];
    for (int i = 0; i < elem1.size(); ++i)
    {
        Elem& elem = elem1[i];
        if(curVolume != elem.vol)
        {
            curVolume = elem.vol;
            dest.write(QString("*ELEMENT,TYPE=C3D4,ELSET=Mould-%1\n").arg(curVolume).toStdString().data());
        }
        QString tmp = QString("%1, %2, %3, %4, %5\n").arg(i + addElem);

        for (int j = 0; j < 4; ++j)
        {
            int& nodeRef = elem.nodes[j];
            Node& node = nodes1[nodeRef - 1];
            !node.original ?
                        nodeRef = node.ref :
                    nodeRef += addNode;
            tmp = tmp.arg(nodeRef);
        }
        dest.write(tmp.toStdString().data());
    }


    dest.write("*****\n");

    QMessageBox::information(0, ":)", "ЫыЫ!!!");

    return 0;
}
