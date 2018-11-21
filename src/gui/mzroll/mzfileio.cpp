#include "mzfileio.h"
#include "projectdatabase.h"
#include <QStringList>
#include <QTextStream>

#include <omp.h>

#include <MavenException.h>
#include <errorcodes.h>

mzFileIO::mzFileIO(QWidget*) {
    sampleId = 0;
    _mainwindow = NULL;
    _stopped = true;
    process = NULL;
    _currentProject = nullptr;

    connect(this,
            SIGNAL(sampleLoaded()),
            this,
            SLOT(_postSampleLoadOperations()));
}

void mzFileIO::setMainWindow(MainWindow* mw) {

    _mainwindow=mw;
    //connect(this,SIGNAL(finished()),_mainwindow,SLOT(setupSampleColors()));
    //connect(this,SIGNAL(finished()),_mainwindow->projectDockWidget,SLOT(updateSampleList()));
   
	//if ( mw->srmDockWidget->isVisible()) connect(this,SIGNAL(finished()),_mainwindow,SLOT(showSRMList()));
    
	//process = new QProcess();
 	//connect(process, SIGNAL(finished(int)), this, SLOT(readProcessOutput(int)));
    //connect(process, SIGNAL(readyReadStandardOutput()), this, SLOT(readThermoRawFileImport()));

}

void mzFileIO::loadSamples(QStringList& files) {
	Q_FOREACH(QString file, files){ addFileToQueue(file); }
	if (filelist.size() > 0) start();
    //setFileList(filenames);
    //start();
}

mzSample* mzFileIO::loadSample(const QString& filename){

    //check if file exists
    QFile file(filename);
    QString sampleName = file.fileName();	//only name of the file, without folder location

    if (!file.exists() ) { 	//couldn't find this file.. check local directory
        return nullptr;
    }

    sampleName.replace(QRegExp(".*/"),"");
    sampleName.replace(".mzCSV","",Qt::CaseInsensitive);
    sampleName.replace(".mzdata","",Qt::CaseInsensitive);
    sampleName.replace(".mzXML","",Qt::CaseInsensitive);
    sampleName.replace(".mzML","",Qt::CaseInsensitive);
    sampleName.replace(".mz5","",Qt::CaseInsensitive);
    sampleName.replace(".pepXML","",Qt::CaseInsensitive);
    sampleName.replace(".xml","",Qt::CaseInsensitive);
    sampleName.replace(".cdf","",Qt::CaseInsensitive);
    sampleName.replace(".raw","",Qt::CaseInsensitive);

    if (sampleName.isEmpty()) return NULL;
    mzSample* sample = NULL;

    if (filename.contains("mzdata",Qt::CaseInsensitive)) {
        // mzFileIO::loadPepXML(filename);
        sample = mzFileIO::parseMzData(filename);
    } else  if (filename.endsWith("raw",Qt::CaseInsensitive)) {
        mzFileIO::ThermoRawFileImport(filename);
    } else {
        sample = new mzSample();
        sample->loadSample( filename.toLatin1().data() );
        if ( sample->scans.size() == 0 ) { delete(sample); sample=NULL; }
    }



    if ( sample && sample->scans.size() > 0 ) {
        if (sample->sampleNumber > 0){
            qDebug() << sampleName;
            QString sampleNumberInfo = " | Sample Number=" + QString::number(sample->sampleNumber);
            sampleName = sampleName + sampleNumberInfo;
        }

        sample->sampleName = string( sampleName.toLatin1().data() );
        
        mtxSampleId.lock();
        sample->setSampleId(++sampleId);
        mtxSampleId.unlock();

        return sample;
    }
    return NULL;
}

int mzFileIO::loadMassBankLibrary(QString fileName) {
    qDebug() << "Loading Nist Libary: " << fileName;
    QFile data(fileName);

    if (!data.open(QFile::ReadOnly) ) {
        qDebug() << "Can't open " << fileName; 
        return 0;
    }

    string dbfilename = fileName.toStdString();
    string dbname = mzUtils::cleanFilename(dbfilename);

   QTextStream stream(&data);

   /* sample
ACCESSION: PR100458
RECORD_TITLE: Cyanidin-3-O-(2''-O-beta-glucopyranosyl-beta-glucopyranoside); LC-ESI-QTOF; MS2; CE:Ramp 5-60 V; [M]+
CH$NAME: Cyanidin-3-O-(2''-O-beta-glucopyranosyl-beta-glucopyranoside)
CH$NAME: Cy 3-Soph
CH$NAME: cyanidin-3-sophoroside
CH$COMPOUND_CLASS: Anthocyanidin
CH$FORMULA: C27H31O16
CH$EXACT_MASS: 611.16121
CH$SMILES: c(c(c([o+1]4)c(cc(c(O)5)c(cc(O)c5)4)OC(O2)C(OC(O3)C(C(C(C3CO)O)O)O)C(C(C2CO)O)O)1)c(O)c(O)cc1
CH$IUPAC: InChI=1S/C27H30O16/c28-7-17-19(34)21(36)23(38)26(41-17)43-25-22(37)20(35)18(8-29)42-27(25)40-16-6-11-13(32)4-10(30)5-15(11)39-24(16)9-1-2-12(31)14(33)3-9/h1-6,17-23,25-29,34-38H,7-8H2,(H3-,30,31,32,33)/p+1/t17-,18-,19-,20-,21+,22+,23-,25-,26+,27-/m1/s1
CH$LINK: CAS 38820-68-7
CH$LINK: CHEMSPIDER 9344547
CH$LINK: KEGG C16306
CH$LINK: KNAPSACK C00006658
CH$LINK: PUBCHEM CID:11169452
PK$NUM_PEAK: 4
PK$PEAK: m/z int. rel.int.
  213.0567 47.98 11
  287.0564 4418 999
  288.0616 60.38 14
  611.1612 927.9 210
//
   */

   QRegExp whiteSpace("\\s+");
   QRegExp formulaMatch("(C\\d+H\\d+\\S*)");

   int charge=0;
   QString line;
   QString id,name, comment,formula,title;
   QStringList compound_class;
   QStringList alias;
   double mw=0;
   double precursor=0;
   int peaks=0;
   vector<float>mzs;
   vector<float>intest;

   int compoundCount=0;

    do {
        line = stream.readLine();

        if(line.startsWith("//",Qt::CaseInsensitive) && !name.isEmpty()) {
            if (!name.isEmpty()) { //insert new compound
               Compound* cpd = new Compound( id.toStdString(), name.toStdString(), formula.toStdString(), charge);
               cpd->precursorMz=precursor;
               cpd->db=dbname;
               cpd->fragment_mzs = mzs;
               cpd->fragment_intensity = intest;
			   Q_FOREACH (QString cat, compound_class) { cpd->category.push_back(cat.toStdString()); }
               DB.addCompound(cpd);
               compoundCount++;
            }

            //reset for the next record
           name = comment = formula = title=QString();
		   compound_class = alias = QStringList();
           mw=precursor=0;
           peaks=0;
           mzs.clear();
           intest.clear();
        }

         if(line.startsWith("ACCESSION:",Qt::CaseInsensitive)) {
             id = line.mid(10,line.length()).simplified();
			 //qDebug() << "ID=" << id;
         } else if (line.startsWith("CH$NAME:",Qt::CaseInsensitive)) {
             QString aliasname = line.mid(9,line.length()).simplified();
			 alias << aliasname;
			 if (name.isEmpty() ) name = aliasname;
			 //qDebug() << "NAME=" << name;
         } else if (line.startsWith("CH$COMPOUND_CLASS:",Qt::CaseInsensitive)) {
             QString comp_class = line.mid(19,line.length()).simplified();
			 compound_class << comp_class;
         } else if (line.startsWith("CH$EXACT_MASS:",Qt::CaseInsensitive)) {
             precursor = line.mid(14,line.length()).simplified().toDouble();
			 //qDebug() << "PRECURSOR=" << precursor;
         } else if (line.startsWith("CH$FORMULA:",Qt::CaseInsensitive)) {
            formula = line.mid(12,line.length()).simplified();
			 //qDebug() << "FORMULA=" << formula;
         } else if (line.startsWith("PK$NUM_PEAK:",Qt::CaseInsensitive)) {
             peaks = line.mid(12,line.length()).simplified().toInt();
			 //qDebug() << "NUM_PEAK=" << peaks;
         } else if (line.startsWith("RECORD_TITLE:",Qt::CaseInsensitive)) {
             title = line.mid(13,line.length()).simplified();
         } else if (line.startsWith("PK$PEAK:",Qt::CaseInsensitive)) {
			 continue;
         } else if ( peaks != 0 ) {
			 line = line.simplified();
             QStringList mzintpair = line.split(whiteSpace);
             if( mzintpair.size() >=2 ) {
                 bool ok=false; bool ook=false;
                 float mz = mzintpair.at(0).toDouble(&ok);
                 float ints = mzintpair.at(1).toDouble(&ook);
                 if (ok && ook && mz >= 0 && ints >= 0) {
                     mzs.push_back(mz); intest.push_back(ints);
			 		 //qDebug() << "PEAK=" << mz << ints;
                 }
             }
         }
    } while (!line.isNull());
    return compoundCount;
}
//TODO: Shouldnot be here
int mzFileIO::loadNISTLibrary(QString fileName) {
    qDebug() << "Loading Nist Libary: " << fileName;
    QFile data(fileName);

    if (!data.open(QFile::ReadOnly) ) {
        qDebug() << "Can't open " << fileName; 
        return 0;
    }

    string dbfilename = fileName.toStdString();
    string dbname = mzUtils::cleanFilename(dbfilename);

   QTextStream stream(&data);

   /* sample
   Name: DGDG 8:0; [M-H]-; DGDG(2:0/6:0)
   MW: 555.22888
   PRECURSORMZ: 555.22888
   Comment: Parent=555.22888 Mz_exact=555.22888 ; DGDG 8:0; [M-H]-; DGDG(2:0/6:0); C23H40O15
   Num Peaks: 2
   115.07586 999 "sn2 FA"
   59.01330 999 "sn1 FA"
   */

   QRegExp whiteSpace("\\s+");
   QRegExp formulaMatch("Formula\\=(C\\d+H\\d+\\S*)");
   QRegExp retentionTimeMatch("AvgRt\\=(\\S+)");

   int charge=0;
   QString line;
   QString name, comment,formula;
   double retentionTime;
   double mw=0;
   double precursor=0;
   int peaks=0;
   vector<float>mzs;
   vector<float>intest;

   int compoundCount=0;

    do {
        line = stream.readLine();

        if(line.startsWith("Name:",Qt::CaseInsensitive) && !name.isEmpty()) {
            if (!name.isEmpty()) { //insert new compound

                Compound* cpd = new Compound(
                           name.toStdString(),
                           name.toStdString(),
                           formula.toStdString(),
                           charge);
			   if (precursor and mw) { cpd->mass=precursor; cpd->precursorMz=precursor; }
			   else if (mw) { cpd->mass=mw; cpd->precursorMz=precursor; }
               //cpd->mass=mw;
               //cpd->precursorMz=mw;
               cpd->db=dbname;
               cpd->fragment_mzs = mzs;
               cpd->fragment_intensity = intest;
			   cpd->expectedRt=retentionTime;
               DB.addCompound(cpd);
               compoundCount++;
            }

            //reset for the next record
           name = comment = formula = QString();
           mw=precursor=0;
		   retentionTime=0;
           peaks=0;
           mzs.clear();
           intest.clear();
        }

         if(line.startsWith("Name:",Qt::CaseInsensitive)) {
             name = line.mid(5,line.length()).simplified();
         } else if (line.startsWith("MW:",Qt::CaseInsensitive)) {
             mw = line.mid(4,line.length()).simplified().toDouble();
         } else if (line.startsWith("PRECURSORMZ:",Qt::CaseInsensitive)) {
             precursor = line.mid(13,line.length()).simplified().toDouble();
         } else if (line.startsWith("Comment:",Qt::CaseInsensitive)) {
             comment = line.mid(8,line.length()).simplified();
             if (comment.contains(formulaMatch)){
                 formula=formulaMatch.capturedTexts().at(1);
                 qDebug() << "Formula=" << formula;
             }
			if (comment.contains(retentionTimeMatch)){
                 retentionTime=retentionTimeMatch.capturedTexts().at(1).simplified().toDouble();
                 //qDebug() << "retentionTime=" << retentionTimeString;
             }
         } else if (line.startsWith("Num Peaks:",Qt::CaseInsensitive) || line.startsWith("NumPeaks:",Qt::CaseInsensitive)) {
            //  peaks = line.mid(11,line.length()).simplified().toInt();
             peaks = 1;
         } else if ( peaks ) {
             QStringList mzintpair = line.split(whiteSpace);
             if( mzintpair.size() >=2 ) {
                 bool ok=false; bool ook=false;
                 float mz = mzintpair.at(0).toDouble(&ok);
                 float ints = mzintpair.at(1).toDouble(&ook);
                 if (ok && ook && mz >= 0 && ints >= 0) {
                     mzs.push_back(mz);
                     intest.push_back(ints);
                 }
             }
         }

    } while (!line.isNull());

    return compoundCount;
}

//TODO: Should not be here
int mzFileIO::loadPepXML(QString fileName) {

    qDebug() << "Loading pepXML sample: " << fileName;
    QFile data(fileName);
    string dbname = mzUtils::cleanFilename(fileName.toStdString());

    if (!data.open(QFile::ReadOnly) ) {
        qDebug() << "Can't open " << fileName; return 0;
    }

    QXmlStreamReader xml(&data);
    xml.setNamespaceProcessing(false);
    QList<QStringRef> taglist;
/*
    <spectrum_query spectrum="BSA_run_120909192952.2.2.2" spectrumNativeID="controllerType=0 controllerNumber=1 scan=2" start_scan="2" end_scan="2" precursor_neutral_mass="887.14544706624" assumed_charge="2" index="2">
      <search_result num_target_comparisons="0" num_decoy_comparisons="0">
        <search_hit hit_rank="1" peptide="SCHTGLGR" peptide_prev_aa="K" peptide_next_aa="S" protein="sp|P02787|TRFE_HUMAN" num_tot_proteins="1" calc_neutral_pep_mass="886.94606" massdiff="-0.19938706624" num_tol_term="2" num_missed_cleavages="0" num_matched_ions="5" tot_num_ions="42">
          <modification_info>
            <mod_aminoacid_mass position="2" mass="160.0306444778"/>
          </modification_info>
          <search_score name="number of matched peaks" value="5"/>
          <search_score name="number of unmatched peaks" value="37"/>
*/
 
    int hitCount=0;
    int charge; 
    float precursorMz;
    int  scannum;

    Compound* cpd = NULL;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
                taglist << xml.name();
                if (xml.name() == "spectrum_query") {
                    scannum = xml.attributes().value("start_scan").toString().toInt();
                    charge = xml.attributes().value("charge").toString().toInt();
                    precursorMz = xml.attributes().value("precursor_neutral_mass").toString().toInt();
		    precursorMz = (precursorMz-charge)/charge;	

                } else if (xml.name() == "search_hit") {
		    hitCount++;
                    int hit_rank = xml.attributes().value("hit_rank").toString().toInt();
                    QString peptide = xml.attributes().value("peptide").toString();
                    QString protein = xml.attributes().value("protein").toString();
		    QString formula = "";

		    cpd = new Compound(
				    protein.toStdString() + "_" + peptide.toStdString(),
				    peptide.toStdString(),
				    formula.toStdString(),
				    charge);

		    cpd->mass=precursorMz;
		    cpd->precursorMz=precursorMz;
		    cpd->db=dbname;
		    //cpd->fragment_mzs = mzs;
		    //cpd->fragment_intensity = intest;
		    DB.addCompound(cpd);

                } else if (xml.name() == "mod_aminoacid_mass" ) {
                    int pos =          xml.attributes().value("position").toString().toInt();
		    double massshift = xml.attributes().value("mass").toString().toDouble();
                } else if (xml.name() == "search_score" ) {
                    QString name = xml.attributes().value("name").toString();
                    QString value = xml.attributes().value("value").toString();
                }
        } else if (xml.isEndElement()) {
               if (!taglist.isEmpty()) taglist.pop_back();
               if (xml.name() == "search_hit") {

               }
        }
    }

    data.close();
    return hitCount;
}
//TODO: should not be here
mzSample* mzFileIO::parseMzData(QString fileName) {

    qDebug() << "Loading mzData sample: " << fileName;
    QFile data(fileName);

    if (!data.open(QFile::ReadOnly) ) {
        qDebug() << "Can't open " << fileName; return NULL;
    }

    QXmlStreamReader xml(&data);
    xml.setNamespaceProcessing(false);
    QList<QStringRef> taglist;

    int scannum=0;

    mzSample* currentSample=NULL;
    Scan* currentScan=NULL;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
                taglist << xml.name();
                if (xml.name() == "spectrum") {
                    scannum++;
                    if (!currentSample) currentSample = new mzSample();
                    currentScan = new Scan(currentSample,scannum,1,0,0,0);
                } else if (xml.name() == "cvParam" && currentScan) {
                    QString _name = xml.attributes().value("name").toString();
                    QString _value = xml.attributes().value("value").toString();
                   //qDebug() << _name << "->" << _value;

                    if(_name.contains("TimeInMinutes",Qt::CaseInsensitive))  currentScan->rt = _value.toFloat();
                    else if(_name.contains("time in seconds",Qt::CaseInsensitive))  currentScan->rt = _value.toFloat();
                    else if(_name.contains("Polarity",Qt::CaseInsensitive)) {
                        if ( _value[0] == 'p' || _value[0] == 'P') {
                            currentScan->setPolarity(+1);
                        } else {
                            currentScan->setPolarity(-1);
                        }
                    }

                    if (_name.contains("MassToChargeRatio",Qt::CaseInsensitive)) {
                        currentScan->precursorMz = _value.toFloat();
                    }

                    if (_name.contains("CollisionEnergy",Qt::CaseInsensitive)) {
                        currentScan->collisionEnergy = _value.toFloat();
                    }

                } else if (xml.name() == "spectrumInstrument" && currentScan) {
                    currentScan->mslevel = xml.attributes().value("msLevel").toString().toInt();
                    if (currentScan->mslevel <= 0 ) currentScan->mslevel=1;
                 } else if (xml.name() == "data" && taglist.size() >= 2 && currentScan) {
                     int precision = xml.attributes().value("precision").toString().toInt();


                     if (taglist.at(taglist.size()-2) == "mzArrayBinary") {
                      currentScan->mz=
                               base64::decode_base64(xml.readElementText().toStdString(),precision/8,false,false);
                     }

                     if (taglist.at(taglist.size()-2) == "intenArrayBinary") {
                        currentScan->intensity =
                                base64::decode_base64(xml.readElementText().toStdString(),precision/8,false,false);
                     }
                }

        } else if (xml.isEndElement()) {
               if (!taglist.isEmpty()) taglist.pop_back();
               if (xml.name() == "spectrum" && currentScan) {
                   currentSample->addScan(currentScan);
                   Q_EMIT (updateProgressBar( "FileImport", scannum%100, 110));
               }
        }
    }

    data.close();
    return currentSample;
}

void mzFileIO::run(void) {

    try {
        fileImport();
    }
    catch (std::exception& excp) {

        // ask user to send back the logs
    }
    catch (...) {
        // ask user to send back the logs
        qDebug() << "uploading samples failed";
    }


    quit();
}

void mzFileIO::fileImport(void) {
    if ( filelist.size() == 0 ) return;
    Q_EMIT (updateProgressBar( "Importing files", 0, filelist.size()));

    _mainwindow->getProjectWidget()->boostSignal.connect(boost::bind(&mzFileIO::qtSlot, this, _1, _2, _3));
    QStringList samples;
    QStringList peaks;
    QStringList projects;
    QStringList spectralhits;

    Q_FOREACH(QString filename, filelist ) {
        try {
            QFileInfo fileInfo(filename);
            if (!fileInfo.exists())
                throw MavenException(ErrorMsg::FileNotFound);

            if (isSampleFileType(filename)) {
                samples << filename;
            } else if (isProjectFileType(filename)) {
                projects << filename;
            } else if (isPeakListType(filename)) {
                peaks << filename;
            } else if (isSpectralHitType(filename)) {
                spectralhits << filename;
            }
            else
                throw MavenException(ErrorMsg::UnsupportedFormat);
        }

        catch (MavenException& excp) {
            qDebug() << "Error: " << excp.what();
        }
    }

    Q_FOREACH(QString filename, projects ) {
        // Since SQLite projects themselves use this thread to load samples
        // it cannot be used to load the project itself. We only queue mzroll
        // files to be loaded in this method.
        if (isMzRollProject(filename)) {
            _mainwindow->ligandWidget->loadCompoundDBMzroll(filename);
            _mainwindow->projectDockWidget->loadMzRollProject(filename);
            QRegExp rxtable("*_table\-[0-9]*");
            rxtable.setPatternSyntax(QRegExp::Wildcard);
            QRegExp rxbm("*_bookmarkedPeaks*");
            rxbm.setPatternSyntax(QRegExp::Wildcard);
            if(rxtable.exactMatch(filename)) {
                Q_EMIT(createPeakTableSignal(filename));
            } else {
                auto groups = readGroupsXML(filename);
                for (auto group : groups) {
                    _mainwindow->bookmarkedPeaks->addPeakGroup(group);
                }
                _mainwindow->bookmarkedPeaks->showAllGroups();
            }
        }
    }

    Q_FOREACH(QString filename, peaks ) {
        QFileInfo fileInfo(filename);
        TableDockWidget* tableX = _mainwindow->addPeaksTable("Group Set " + fileInfo.fileName());
        auto groups = readGroupsXML(filename);
        for (auto group : groups) {
            _mainwindow->bookmarkedPeaks->addPeakGroup(group);
        }
        _mainwindow->bookmarkedPeaks->showAllGroups();
    }

    // check if the user is trying to upload any cdf file. if, yes then disable
    // mulitprocessing. store the value in temp and restore it once done
    int uploadMultiprocessing = _mainwindow->getSettings()->value("uploadMultiprocessing").toInt();
    Q_FOREACH(const QString& filename, samples) {
      if(filename.endsWith(".nc", Qt::CaseInsensitive) || filename.endsWith(".cdf", Qt::CaseInsensitive)) {
          uploadMultiprocessing = 0;
          break;
      }
    }
    qDebug() << "uploadMultiprocessing: " <<  uploadMultiprocessing << endl;
    if (uploadMultiprocessing) {
        int iter = 0;
        #pragma omp parallel for shared(iter)
        for (int i = 0; i < samples.size(); i++) {
            QString filename = samples.at(i);
            mzSample* sample = loadSample(filename);
            if (sample && sample->scans.size() > 0)
                emit addNewSample(sample);

            #pragma omp atomic
            iter++;

            Q_EMIT (updateProgressBar( tr("Importing file %1").arg(filename), iter, samples.size()));
        }
    } else {
        int iter = 0;
        for (int i = 0; i < samples.size(); i++) {
            QString filename = samples.at(i);
            mzSample* sample = loadSample(filename);
            if (sample && sample->scans.size() > 0)
                _mainwindow->addSample(sample);

            iter++;

            Q_EMIT (updateProgressBar( tr("Importing file %1").arg(filename), iter, samples.size()));

        }
    }

    Q_FOREACH(QString filename, spectralhits ) {
        if (filename.contains("pepXML",Qt::CaseInsensitive)) {
            _mainwindow->spectralHitsDockWidget->loadPepXML(filename);
        }
        else if (filename.contains("pep.xml",Qt::CaseInsensitive)) {
             _mainwindow->spectralHitsDockWidget->loadPepXML(filename);
        }
        else if (filename.contains("idpDB",Qt::CaseInsensitive)) {
             _mainwindow->spectralHitsDockWidget->loadIdPickerDB(filename);
        }
   }

    //done..
    Q_EMIT (updateProgressBar( "Done importing", samples.size(), samples.size()));
    if (samples.size() > 0)       Q_EMIT(sampleLoaded());
    if (spectralhits.size() >0)   Q_EMIT(spectraLoaded());
    if (projects.size() >0)       Q_EMIT(projectLoaded());
    if (peaks.size() > 0)    	  Q_EMIT(peaklistLoaded());
    filelist.clear(); //empty queue
}

void mzFileIO::qtSlot(const string& progressText, unsigned int completed_samples, int total_samples)
{
        Q_EMIT(updateProgressBar(QString::fromStdString(progressText), completed_samples, total_samples));

}


bool mzFileIO::isKnownFileType(QString filename) {
    if (isSampleFileType(filename))  return true;
    if (isProjectFileType(filename)) return true;
    if (isSpectralHitType(filename)) return true;
    if (isPeakListType(filename)) return true;
    return false;
}

bool mzFileIO::isSampleFileType(QString filename) {
    QStringList extList;
    extList << "mzXML" << "cdf" << "nc" << "mzML" << "mzData" << "mzML";
    Q_FOREACH (QString suffix, extList) {
        if (filename.endsWith(suffix,Qt::CaseInsensitive)) return true;
    }
    return false;
}

bool mzFileIO::isProjectFileType(QString filename) {
    return (isMzRollProject(filename) || isSQLiteProject(filename));
}

bool mzFileIO::isMzRollProject(QString filename)
{
    if (filename.endsWith("mzroll", Qt::CaseInsensitive))
        return true;
    return false;
}

bool mzFileIO::isSQLiteProject(QString filename)
{
    if (filename.endsWith("emDB", Qt::CaseInsensitive))
        return true;
    return false;
}

bool mzFileIO::sqliteProjectIsOpen()
{
    return _currentProject != nullptr;
}

void mzFileIO::closeSQLiteProject()
{
    if (!_currentProject)
        return;

    delete _currentProject;
    _currentProject = nullptr;
}

int mzFileIO::writeBookmarkedGroup(PeakGroup* group)
{
    if (_currentProject)
        return _currentProject->saveGroupAndPeaks(group, 0, "Bookmarks");
    else
        return -1;
}

bool mzFileIO::writeSQLiteProject(QString filename)
{
    if (filename.isEmpty())
        return false;
    qDebug() << "Debug: saving SQLite project " << filename << endl;

    std::vector<mzSample*> sampleSet = _mainwindow->getSamples();
    if (sampleSet.size() == 0)
        return false;

    if (_currentProject
            and _currentProject->projectName() == filename.toStdString()) {
        qDebug() << "Debug: Saving in existing project…";
    } else {
        qDebug() << "Debug: Creating new project to save…";
        _currentProject = new ProjectDatabase(filename.toStdString());
    }

    if (_currentProject) {
        _currentProject->deleteAll();  // this is crazy
        _currentProject->saveSamples(sampleSet);
        _currentProject->saveAlignment(sampleSet);

        set<Compound*> compoundSet;
        int topLevelGroupCount = 0;
        auto allTablesList = _mainwindow->getPeakTableList();
        allTablesList.push_back(_mainwindow->bookmarkedPeaks);
        for (const auto& peakTable : allTablesList) {
            for (PeakGroup* group : peakTable->getGroups()) {
                topLevelGroupCount++;
                string tableName = peakTable->windowTitle().toStdString();
                _currentProject->saveGroupAndPeaks(group,
                                                   0,
                                                   tableName);
                if (group->compound)
                    compoundSet.insert(group->compound);
            }
        }
        _currentProject->saveCompounds(compoundSet);
        return true;
    }
    qDebug() << "Error: Cannot write to closed project" << filename;
    return false;
}

bool mzFileIO::readSQLiteProject(QString fileName)
{
    if (_currentProject)
        closeSQLiteProject();

    if (_currentProject)
        return false;

    _currentProject = new ProjectDatabase(fileName.toStdString());

    // load compounds stored in the project file
    auto compounds = _currentProject->loadCompounds();
    for (auto compound : compounds)
        DB.addCompound(compound);

    auto newSamples =
        _currentProject->loadSampleNames(_mainwindow->getSamples());
    for (auto& sampleName : newSamples) {
        // TODO: in the original implementation, the author (Eugene) prompts the
        // the user to locate each file that was not automatically found.
        addFileToQueue(QString::fromStdString(sampleName));
    }

    // start sample loading threaded operation
    start();
    return true;
}

void mzFileIO::readAllPeakTablesSQLite(const vector<mzSample*> newSamples)
{
    if (!_currentProject)
        return;

    // lambda function to check for existence of peak table by a certain name
    auto findPeakTable = [](QList<QPointer<TableDockWidget>> tableList,
                            string tableName) {
        for (auto table : tableList)
            if (table->windowTitle().toStdString() == tableName)
                return static_cast<TableDockWidget *>(table);
        return static_cast<TableDockWidget *>(nullptr);
    };

    // create tables for search results
    auto bookmarkTableTitle = "Bookmark Table";
    auto tableNames = _currentProject->getTableNames();
    for (auto tableName : tableNames) {
        TableDockWidget* table = findPeakTable(_mainwindow->getPeakTableList(),
                                               tableName);
        if (!table && tableName != bookmarkTableTitle)
            _mainwindow->addPeaksTable(QString::fromStdString(tableName));
    }

    // load all peakgroups
    auto groups = _currentProject->loadGroups(newSamples);
    for (auto& group : groups) {
        if (group->searchTableName.empty())
            group->searchTableName = bookmarkTableTitle;

        auto allTablesList = _mainwindow->getPeakTableList();
        allTablesList.push_back(_mainwindow->bookmarkedPeaks);
        TableDockWidget* table = findPeakTable(allTablesList,
                                               group->searchTableName);
        if (table)
            table->addPeakGroup(group);
    }

    // updated display widgets
    for (auto& table : _mainwindow->getPeakTableList())
        table->showAllGroups();
    _mainwindow->bookmarkedPeaks->showAllGroups();
}

void mzFileIO::_postSampleLoadOperations()
{
    if (!_currentProject)
        return;

    auto samples = _mainwindow->getSamples();
    _currentProject->updateSamples(samples);
    _currentProject->loadAndPerformAlignment(samples);
    readAllPeakTablesSQLite(samples);
}

void mzFileIO::markv_0_1_5mzroll(QString fileName)
{
    mzrollv_0_1_5 = true;

    QFile data(fileName);

    if (!data.open(QFile::ReadOnly)) {
        return;
    }

    QXmlStreamReader xml(&data);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.name() == "SamplesUsed") {
                // mark false if <SamplesUsed> which is only in new version
                mzrollv_0_1_5 = false;
                break;
            }
        }
    }

    data.close();

    return;
}

void mzFileIO::cleanString(QString& name)
{
    name.replace('#', '_');
    name = 's' + name;
}

void mzFileIO::writeGroupXML(QXmlStreamWriter& stream, PeakGroup* group)
{
    if (!group)
        return;

    stream.writeStartElement("PeakGroup");
    stream.writeAttribute("groupId", QString::number(group->groupId));
    stream.writeAttribute("tagString", QString(group->tagString.c_str()));
    stream.writeAttribute("metaGroupId", QString::number(group->metaGroupId));
    stream.writeAttribute("clusterId", QString::number(group->clusterId));
    stream.writeAttribute("expectedRtDiff",
                          QString::number(group->expectedRtDiff, 'f', 6));
    stream.writeAttribute("groupRank", QString::number(group->groupRank, 'f', 6));
    stream.writeAttribute("expectedMz", QString::number(group->expectedMz, 'f', 6));
    stream.writeAttribute("label", QString::number(group->label));
    stream.writeAttribute("type", QString::number((int)group->type()));
    stream.writeAttribute("changeFoldRatio",
                          QString::number(group->changeFoldRatio, 'f', 6));
    stream.writeAttribute("changePValue",
                          QString::number(group->changePValue, 'e', 6));
    if (group->srmId.length())
        stream.writeAttribute("srmId", QString(group->srmId.c_str()));

    // for sample contrasts  ratio and pvalue
    if (group->hasCompoundLink()) {
        Compound* c = group->compound;
        stream.writeAttribute("compoundId", QString(c->id.c_str()));
        stream.writeAttribute("compoundDB", QString(c->db.c_str()));
        stream.writeAttribute("compoundName", QString(c->name.c_str()));
        stream.writeAttribute("formula", QString(c->id.c_str()));
    }

    stream.writeStartElement("SamplesUsed");
    for (int i = 0; i < group->samples.size(); ++i) {
        stream.writeStartElement("sample");
        stream.writeAttribute("id",
                              QString::number(group->samples[i]->getSampleId()));
        stream.writeEndElement();
    }
    stream.writeEndElement();

    for (int j = 0; j < group->peaks.size(); j++) {
        Peak& p = group->peaks[j];
        stream.writeStartElement("Peak");
        stream.writeAttribute("pos", QString::number(p.pos, 'f', 6));
        stream.writeAttribute("minpos", QString::number(p.minpos, 'f', 6));
        stream.writeAttribute("maxpos", QString::number(p.maxpos, 'f', 6));
        stream.writeAttribute("splineminpos",
                              QString::number(p.splineminpos, 'f', 6));
        stream.writeAttribute("splinemaxpos",
                              QString::number(p.splinemaxpos, 'f', 6));
        stream.writeAttribute("rt", QString::number(p.rt, 'f', 6));
        stream.writeAttribute("rtmin", QString::number(p.rtmin, 'f', 6));
        stream.writeAttribute("rtmax", QString::number(p.rtmax, 'f', 6));
        stream.writeAttribute("mzmin", QString::number(p.mzmin, 'f', 6));
        stream.writeAttribute("mzmax", QString::number(p.mzmax, 'f', 6));
        stream.writeAttribute("scan", QString::number(p.scan));
        stream.writeAttribute("minscan", QString::number(p.minscan));
        stream.writeAttribute("maxscan", QString::number(p.maxscan));
        stream.writeAttribute("peakArea", QString::number(p.peakArea, 'f', 6));
        stream.writeAttribute("peakSplineArea",
                              QString::number(p.peakSplineArea, 'f', 6));
        stream.writeAttribute("peakAreaCorrected",
                              QString::number(p.peakAreaCorrected, 'f', 6));
        stream.writeAttribute("peakAreaTop",
                              QString::number(p.peakAreaTop, 'f', 6));
        stream.writeAttribute("peakAreaTopCorrected",
                              QString::number(p.peakAreaTopCorrected, 'f', 6));
        stream.writeAttribute("peakAreaFractional",
                              QString::number(p.peakAreaFractional, 'f', 6));
        stream.writeAttribute("peakRank", QString::number(p.peakRank, 'f', 6));
        stream.writeAttribute("peakIntensity",
                              QString::number(p.peakIntensity, 'f', 6));

        stream.writeAttribute("peakBaseLineLevel",
                              QString::number(p.peakBaseLineLevel, 'f', 6));
        stream.writeAttribute("peakMz", QString::number(p.peakMz, 'f', 6));
        stream.writeAttribute("medianMz", QString::number(p.medianMz, 'f', 6));
        stream.writeAttribute("baseMz", QString::number(p.baseMz, 'f', 6));
        stream.writeAttribute("quality", QString::number(p.quality, 'f', 6));
        stream.writeAttribute("width", QString::number(p.width, 'f', 6));
        stream.writeAttribute("gaussFitSigma",
                              QString::number(p.gaussFitSigma, 'f', 6));
        stream.writeAttribute("gaussFitR2",
                              QString::number(p.gaussFitR2, 'f', 6));
        stream.writeAttribute("groupNum", QString::number(p.groupNum));
        stream.writeAttribute("noNoiseObs", QString::number(p.noNoiseObs));
        stream.writeAttribute("noNoiseFraction",
                              QString::number(p.noNoiseFraction, 'f', 6));
        stream.writeAttribute("symmetry", QString::number(p.symmetry, 'f', 6));
        stream.writeAttribute("signalBaselineRatio",
                              QString::number(p.signalBaselineRatio, 'f', 6));
        stream.writeAttribute("groupOverlap",
                              QString::number(p.groupOverlap, 'f', 6));
        stream.writeAttribute("groupOverlapFrac",
                              QString::number(p.groupOverlapFrac, 'f', 6));
        stream.writeAttribute("localMaxFlag", QString::number(p.localMaxFlag));
        stream.writeAttribute("fromBlankSample",
                              QString::number(p.fromBlankSample));
        stream.writeAttribute("label", QString::number(p.label));
        stream.writeAttribute("sample",
                              QString(p.getSample()->sampleName.c_str()));
        stream.writeEndElement();
    }

    if (group->childCount()) {
        stream.writeStartElement("children");
        for (int i = 0; i < group->children.size(); i++) {
            PeakGroup* child = &(group->children[i]);
            writeGroupXML(stream, child);
        }
        stream.writeEndElement();
    }
    stream.writeEndElement();
}

void mzFileIO::writeGroupsXML(QXmlStreamWriter& stream,
                              vector<PeakGroup>& groups)
{
    if (groups.size()) {
        stream.writeStartElement("PeakGroups");
        for (auto& group : groups)
            writeGroupXML(stream, &group);
        stream.writeEndElement();
    }
}

void mzFileIO::readSamplesXML(QXmlStreamReader& xml,
                              PeakGroup* group,
                              float mzrollVersion)
{
    vector<mzSample*> samples = _mainwindow->getSamples();

    if (mzrollVersion == 1) {
        if (xml.name() == "SamplesUsed") {
            xml.readNextStartElement();
            while (xml.name() == "sample") {
                unsigned int id =
                    xml.attributes().value("id").toString().toInt();
                for (int i = 0; i < samples.size(); ++i) {
                    mzSample* sample = samples[i];
                    if (id == sample->getSampleId()) {
                        group->samples.push_back(sample);
                    }
                }
                xml.readNextStartElement();
            }
        }
    } else {
        for (int i = 0; i < samples.size(); ++i) {
            QString name = QString::fromStdString(samples[i]->sampleName);
            cleanString(name);
            if (xml.name() == "PeakGroup" && mzrollv_0_1_5
                && samples[i]->isSelected) {
                /**
                 * if mzroll is from old version, just insert sample in group
                 * from checking whether it is selected or not at time of
                 * exporting. This can give erroneous result for old version if
                 * at time of exporting mzroll user has selected diffrent
                 * samples from samples were used at time of peak finding which
                 * was inherent problem of old version of ElMaven.
                 */
                group->samples.push_back(samples[i]);
            } else if (xml.name() == "SamplesUsed"
                       && xml.attributes().value(name).toString() == "Used") {
                /**
                 * if mzroll file is of new version, it's sample name will
                 * precede by 's' and has value of <Used> or <NotUsed>
                 */
                group->samples.push_back(samples[i]);
            }
        }
    }
}

PeakGroup* mzFileIO::readGroupXML(QXmlStreamReader& xml, PeakGroup* parent)
{
    PeakGroup* group = new PeakGroup();

    group->groupId = xml.attributes().value("groupId").toString().toInt();
    group->tagString =
        xml.attributes().value("tagString").toString().toStdString();
    group->metaGroupId =
        xml.attributes().value("metaGroupId").toString().toInt();
    group->clusterId = xml.attributes().value("clusterId").toString().toInt();
    group->expectedRtDiff =
        xml.attributes().value("expectedRtDiff").toString().toFloat();
    group->groupRank = xml.attributes().value("grouRank").toString().toFloat();
    group->expectedMz =
        xml.attributes().value("expectedMz").toString().toFloat();
    group->label = xml.attributes().value("label").toString().toInt();
    group->setType((PeakGroup::GroupType)xml.attributes()
                       .value("type")
                       .toString()
                       .toInt());
    group->changeFoldRatio =
        xml.attributes().value("changeFoldRatio").toString().toFloat();
    group->changePValue =
        xml.attributes().value("changePValue").toString().toFloat();

    string compoundId =
        xml.attributes().value("compoundId").toString().toStdString();
    string compoundDB =
        xml.attributes().value("compoundDB").toString().toStdString();
    string compoundName =
        xml.attributes().value("compoundName").toString().toStdString();

    string srmId = xml.attributes().value("srmId").toString().toStdString();
    if (!srmId.empty())
        group->setSrmId(srmId);

    if (!compoundName.empty() && !compoundDB.empty()) {
        vector<Compound*> matches =
            DB.findSpeciesByName(compoundName, compoundDB);
        if (matches.size() > 0)
            group->compound = matches[0];
    } else if (!compoundId.empty()) {
        Compound* c = DB.findSpeciesById(compoundId, DB.ANYDATABASE);
        if (c)
            group->compound = c;
    }

    if (!group->compound) {
        if (!compoundId.empty())
            group->tagString = compoundId;
        else if (!compoundName.empty())
            group->tagString = compoundName;
    }

    if (parent) {
        parent->addChild(*group);
        if (parent->childCount() > 0)
            group = &(parent->children[parent->children.size() - 1]);
    }

    return group;
}

void mzFileIO::readPeakXML(QXmlStreamReader& xml, PeakGroup* parent)
{
    Peak p;
    p.pos = xml.attributes().value("pos").toString().toInt();
    p.minpos = xml.attributes().value("minpos").toString().toInt();
    p.maxpos = xml.attributes().value("maxpos").toString().toInt();
    p.splineminpos = xml.attributes().value("splineminpos").toString().toInt();
    p.splinemaxpos = xml.attributes().value("splinemaxpos").toString().toInt();
    p.rt = xml.attributes().value("rt").toString().toDouble();
    p.rtmin = xml.attributes().value("rtmin").toString().toDouble();
    p.rtmax = xml.attributes().value("rtmax").toString().toDouble();
    p.mzmin = xml.attributes().value("mzmin").toString().toDouble();
    p.mzmax = xml.attributes().value("mzmax").toString().toDouble();
    p.scan = xml.attributes().value("scan").toString().toInt();
    p.minscan = xml.attributes().value("minscan").toString().toInt();
    p.maxscan = xml.attributes().value("maxscan").toString().toInt();
    p.peakArea = xml.attributes().value("peakArea").toString().toDouble();
    p.peakSplineArea =
        xml.attributes().value("peakSplineArea").toString().toDouble();
    p.peakAreaCorrected =
        xml.attributes().value("peakAreaCorrected").toString().toDouble();
    p.peakAreaTop = xml.attributes().value("peakAreaTop").toString().toDouble();
    p.peakAreaTopCorrected =
        xml.attributes().value("peakAreaTopCorrected").toString().toDouble();
    p.peakAreaFractional =
        xml.attributes().value("peakAreaFractional").toString().toDouble();
    p.peakRank = xml.attributes().value("peakRank").toString().toDouble();
    p.peakIntensity =
        xml.attributes().value("peakIntensity").toString().toDouble();
    p.peakBaseLineLevel =
        xml.attributes().value("peakBaseLineLevel").toString().toDouble();
    p.peakMz = xml.attributes().value("peakMz").toString().toDouble();
    p.medianMz = xml.attributes().value("medianMz").toString().toDouble();
    p.baseMz = xml.attributes().value("baseMz").toString().toDouble();
    p.quality = xml.attributes().value("quality").toString().toDouble();
    p.width = xml.attributes().value("width").toString().toInt();
    p.gaussFitSigma =
        xml.attributes().value("gaussFitSigma").toString().toDouble();
    p.gaussFitR2 = xml.attributes().value("gaussFitR2").toString().toDouble();
    p.groupNum = xml.attributes().value("groupNum").toString().toInt();
    p.noNoiseObs = xml.attributes().value("noNoiseObs").toString().toInt();
    p.noNoiseFraction =
        xml.attributes().value("noNoiseFraction").toString().toDouble();
    p.symmetry = xml.attributes().value("symmetry").toString().toDouble();
    p.signalBaselineRatio =
        xml.attributes().value("signalBaselineRatio").toString().toDouble();
    p.groupOverlap =
        xml.attributes().value("groupOverlap").toString().toDouble();
    p.groupOverlapFrac =
        xml.attributes().value("groupOverlapFrac").toString().toDouble();
    p.localMaxFlag = xml.attributes().value("localMaxFlag").toString().toInt();
    p.fromBlankSample =
        xml.attributes().value("fromBlankSample").toString().toInt();
    p.label = xml.attributes().value("label").toString().toInt();
    string sampleName =
        xml.attributes().value("sample").toString().toStdString();
    vector<mzSample*> samples = _mainwindow->getSamples();
    for (int i = 0; i < samples.size(); i++) {
        if (samples[i]->sampleName == sampleName) {
            p.setSample(samples[i]);
            break;
        }
    }

    parent->addPeak(p);
}

vector<PeakGroup*> mzFileIO::readGroupsXML(QString fileName)
{
    markv_0_1_5mzroll(fileName);

    QFile data(fileName);
    vector<PeakGroup*> groups;
    if ( !data.open(QFile::ReadOnly) ) {
        cerr << "File open: " << fileName.toStdString() << " failed" << endl;
        return groups;
    }

    QXmlStreamReader xml(&data);
    PeakGroup* group = nullptr;
    PeakGroup* parent = nullptr;
    QStack<PeakGroup*> stack;

    float mzrollVersion = 0;

    while (!xml.atEnd()) {
        if (xml.isStartElement() && xml.name() == "project") {
            mzrollVersion = xml.attributes().value("mzrollVersion").toFloat();
        }
        xml.readNext();
        if (xml.hasError()) {
            qDebug() << "Error in xml reading: " << xml.errorString();
        }
        if (xml.isStartElement()) {
            if (xml.name() == "PeakGroup") {
                group = readGroupXML(xml, parent);
                if (!group->isIsotope())
                    groups.push_back(group);
            }
            if (xml.name() == "SamplesUsed" && group) {
                readSamplesXML(xml, group, mzrollVersion);
            }
            if (xml.name() == "Peak" && group) {
                readPeakXML(xml, group);
            }
            if (xml.name() == "children" && group) {
                stack.push(group);
                parent = stack.top();
            }
        }

        if (xml.isEndElement()) {
           if (xml.name() == "children") {
                if (stack.size() > 0)
                    parent = stack.pop();
                if (parent && parent->childCount()) {
                    for (int i = 0; i < parent->children.size(); i++) {
                        parent->children[i].minQuality =
                            _mainwindow->mavenParameters->minQuality;
                        parent->children[i].groupStatistics();
                    }
                }
                if (stack.size() == 0)
                    parent = nullptr;
            }
            if (xml.name() == "PeakGroup") {
                if (group) {
                    group->minQuality =
                        _mainwindow->mavenParameters->minQuality;
                    group->groupStatistics();
                }
                group = nullptr;
            }
        }
    }
    for (auto group : groups) {
        if (!group)
            continue;
        group->minQuality = _mainwindow->mavenParameters->minQuality;
        group->groupStatistics();
    }
    return groups;
}

bool mzFileIO::isSpectralHitType(QString filename) {
    QStringList extList;
    extList << "pep.xml" << "pepXML" << "idpDB";
    Q_FOREACH (QString suffix, extList) {
        if (filename.endsWith(suffix,Qt::CaseInsensitive)) return true;
    }
    return false;
}

bool mzFileIO::isPeakListType(QString filename) {
    QStringList extList;
    extList << "mzPeaks";
    Q_FOREACH (QString suffix, extList) {
        if (filename.endsWith(suffix,Qt::CaseInsensitive)) return true;
    }
    return false;
}

void mzFileIO::readThermoRawFileImport() {
    if(process) {
        QByteArray data = process->readAllStandardOutput();
        qDebug() << "Captured:" << data;
    }
}

void mzFileIO::addFileToQueue(QString f)
{
//    if (isKnownFileType(f)) filelist << f;
    filelist << f;
}

void mzFileIO::removeAllFilefromQueue() {
    filelist.clear();
}

int mzFileIO::ThermoRawFileImport(QString fileName) {

    if (process->pid()){
           process->terminate();
           qDebug()  <<  "Killing process..\n";
           return -1;
    }

   QString rawExtractExe = _mainwindow->getSettings()->value("RawExtractProgram").toString();
    if(!QFile::exists(rawExtractExe)) {
        qDebug() << "Can't find " + rawExtractExe;
        return -1;
    }

    //start process
    QStringList arguments; arguments  << fileName;
    qDebug() << "Running:" << rawExtractExe << arguments;

    process->start(rawExtractExe, arguments);
}
