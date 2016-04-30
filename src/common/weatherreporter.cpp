/*****************************************************************************
* Copyright 2015-2016 Alexander Barthel albar965@mailbox.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "weatherreporter.h"
#include "logging/loggingdefs.h"
#include "gui/mainwindow.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QNetworkReply>
#include <QTimer>

#include <settings/settings.h>

WeatherReporter::WeatherReporter(MainWindow *parentWindow)
  : QObject(parentWindow)
{
  asnSnapshotPath = getAsnSnapshotPath();
  if(!asnSnapshotPath.isEmpty())
  {
    loadActiveSkySnapshot();
    fsWatcher = new QFileSystemWatcher(this);
    if(fsWatcher->addPath(asnSnapshotPath))
      fsWatcher->connect(fsWatcher, &QFileSystemWatcher::fileChanged, this, &WeatherReporter::fileChanged);
    else
      qWarning() << "cannot watch" << asnSnapshotPath;
  }
}

WeatherReporter::~WeatherReporter()
{
  clearNoaaReply();
  clearVatsimReply();
  delete fsWatcher;
}

// C:\Users\USER\AppData\Roaming\HiFi\ASNFSX\Weather\wx_station_list.txt
// AGGH::AGGH 261800Z 20002KT 9999 FEW014 SCT027 25/24 Q1009::AGGH 261655Z 2618/2718 VRB03KT 9999 FEW017 SCT028 FM270000
// 34010KT 9999 -SH SCT019 SCT03 FM271200 VRB03KT 9999 -SH FEW017 SCT028 PROB30 INTER 2703/27010 5000 TSSH SCT016 FEW017CB BKN028
// T 25 27 31 32 Q 1009 1011 1011 1009::278,11,24.0/267,12,19.0/263,13,16.1/233,12,7.2/290,7,-3.0/338,8,-13.0/348,18,-27.9/9,19,-37.9/26,15,-51.3
void WeatherReporter::loadActiveSkySnapshot()
{
  if(asnSnapshotPath.isEmpty())
    return;

  QFile file(asnSnapshotPath);
  if(file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    asnMetars.clear();

    QTextStream sceneryCfg(&file);

    QString line;
    while(sceneryCfg.readLineInto(&line))
    {
      QStringList list = line.split("::");
      asnMetars.insert(list.at(0), list.at(1));

    }
    file.close();
  }
  else
    qWarning() << "cannot open" << file.fileName() << "reason" << file.errorString();
}

QString WeatherReporter::getAsnSnapshotPath()
{
  // TODO find better way to get to Roaming directory
  QString appdata = atools::settings::Settings::instance().getPath();

  QDir dir(appdata);

  // TODO other simulators
  if(dir.cdUp() && dir.cd("HiFi") && dir.cd("ASNFSX") && dir.cd("Weather"))
  {
    QString file = dir.filePath("current_wx_snapshot.txt");
    if(QFile::exists(file))
      return file;
    else
      qWarning() << "wx_station_list.txt not found";
  }
  else
    qWarning() << "HiFi/ASNFSX/Weather not found";
  return QString();
}

void WeatherReporter::clearVatsimReply()
{
  if(vatsimReply != nullptr)
  {
    disconnect(vatsimReply, &QNetworkReply::finished, this, &WeatherReporter::httpFinishedVatsim);
    vatsimReply->abort();
    vatsimReply->deleteLater();
    vatsimReply = nullptr;
    vatsimRequestIcao.clear();
  }
}

void WeatherReporter::loadVatsimMetar(const QString& airportIcao)
{
  // http://metar.vatsim.net/metar.php?id=EDDF
  qDebug() << "Vatsim Request for" << airportIcao;
  clearVatsimReply();

  vatsimRequestIcao = airportIcao;
  QNetworkRequest request(QUrl("http://metar.vatsim.net/metar.php?id=" + airportIcao));

  vatsimReply = networkManager.get(request);

  if(vatsimReply != nullptr)
    connect(vatsimReply, &QNetworkReply::finished, this, &WeatherReporter::httpFinishedVatsim);
  else
    qWarning() << "Vatsim Reply is null";
  qDebug() << "Vatsim Request for" << airportIcao << "done";
}

void WeatherReporter::clearNoaaReply()
{
  if(noaaReply != nullptr)
  {
    disconnect(noaaReply, &QNetworkReply::finished, this, &WeatherReporter::httpFinishedNoaa);
    noaaReply->abort();
    noaaReply->deleteLater();
    noaaReply = nullptr;
    noaaRequestIcao.clear();
  }
}

void WeatherReporter::loadNoaaMetar(const QString& airportIcao)
{
  // http://www.aviationweather.gov/static/adds/metars/stations.txt
  // http://weather.noaa.gov/pub/data/observations/metar/stations/EDDL.TXT
  // http://weather.noaa.gov/pub/data/observations/metar/
  // request.setRawHeader("User-Agent", "Qt NetworkAccess 1.3");
  qDebug() << "NOAA Request for" << airportIcao;

  clearNoaaReply();

  noaaRequestIcao = airportIcao;
  QNetworkRequest request(QUrl("http://weather.noaa.gov/pub/data/observations/metar/stations/" +
                               airportIcao + ".TXT"));

  noaaReply = networkManager.get(request);

  if(noaaReply != nullptr)
    connect(noaaReply, &QNetworkReply::finished, this, &WeatherReporter::httpFinishedNoaa);
  else
    qWarning() << "NOAA Reply is null";
  qDebug() << "NOAA Request for" << airportIcao << "done";
}

void WeatherReporter::httpFinishedNoaa()
{
  httpFinished(noaaReply, noaaRequestIcao, noaaMetars);
  noaaReply = nullptr;
}

void WeatherReporter::httpFinishedVatsim()
{
  httpFinished(vatsimReply, vatsimRequestIcao, vatsimMetars);
  vatsimReply = nullptr;
}

void WeatherReporter::httpFinished(QNetworkReply *reply, const QString& icao, QHash<QString, QString>& metars)
{
  if(reply != nullptr)
  {
    if(reply->error() == QNetworkReply::NoError)
    {
      QString metar(reply->readAll());
      if(!metar.contains("no metar available", Qt::CaseInsensitive))
        metars.insert(icao, metar);
      else
        metars.insert(icao, QString());
      qDebug() << "Request for" << icao << "succeeded.";
      emit weatherUpdated();
    }
    else if(reply->error() != QNetworkReply::OperationCanceledError)
    {
      metars.insert(icao, QString());
      qDebug() << "Request for" << icao << "failed. Reason:" << reply->errorString();
    }
    reply->deleteLater();
  }
}

QString WeatherReporter::getAsnMetar(const QString& airportIcao)
{
  return asnMetars.value(airportIcao, QString());
}

QString WeatherReporter::getNoaaMetar(const QString& airportIcao)
{
  if(!noaaMetars.contains(airportIcao))
    loadNoaaMetar(airportIcao);
  return noaaMetars.value(airportIcao, QString());
}

QString WeatherReporter::getVatsimMetar(const QString& airportIcao)
{
  if(!vatsimMetars.contains(airportIcao))
    loadVatsimMetar(airportIcao);
  return vatsimMetars.value(airportIcao, QString());

}

void WeatherReporter::fileChanged(const QString& path)
{
  Q_UNUSED(path);
  qDebug() << "file" << path << "changed";
  loadActiveSkySnapshot();
}
