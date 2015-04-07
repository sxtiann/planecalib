/*
 * ARWindow.cpp
 *
 *  Created on: 7.4.2014
 *      Author: dan
 */

#include "MainWindow.h"
#include "planecalib/PlaneCalibSystem.h"
#include "planecalib/Map.h"
#include "planecalib/PoseTracker.h"
#include "planecalib/HomographyCalibration.h"
#include "../PlaneCalibApp.h"
#include "matio.h"

namespace planecalib
{
void MainWindow::showHelp() const
{
	BaseWindow::showHelp();
	MYAPP_LOG << "Shows an overlay of the triangulated features and the cube."
		"Three display modes: show matches, show reprojected features with 3 measurements, and show all reprojected features.\n";
}

bool MainWindow::init(PlaneCalibApp *app, const Eigen::Vector2i &imageSize)
{
	BaseWindow::init(app, imageSize);

	mSystem = &app->getSystem();

	resize();

	//mKeyBindings.addBinding(false, 't', static_cast<KeyBindingHandler<BaseWindow>::SimpleBindingFunc>(&ARWindow::toggleDisplayType), "Toggle display mode.");
	mKeyBindings.addBinding(false, 'l', static_cast<KeyBindingHandler<BaseWindow>::SimpleBindingFunc>(&MainWindow::loadBouguetCalib), "Load bouguet calibration data.");
	mKeyBindings.addBinding(false, 'b', static_cast<KeyBindingHandler<BaseWindow>::SimpleBindingFunc>(&MainWindow::doFullBA), "Full BA.");
	mKeyBindings.addBinding(false, 'h', static_cast<KeyBindingHandler<BaseWindow>::SimpleBindingFunc>(&MainWindow::doHomographyBA), "Homography BA.");
	
	mRefTexture.create(GL_RGB, eutils::ToSize(imageSize));
	mRefTexture.update(mSystem->getMap().getKeyframes()[0]->getColorImage());

	return true;
}

void MainWindow::updateState()
{
	shared_lock<shared_mutex> lockRead(mSystem->getMap().getMutex());

	mTracker = &mSystem->getTracker();
	mMap = &mSystem->getMap();

	//Clear all
	mImagePoints.clear();
	mImagePointColors.clear();
	mImageLines.clear();
	mImageLineColors.clear();

	mTrackerPose = mTracker->getCurrentPose();
	mIsLost = mTracker->isLost();

	//Add features
	//mTrackerPose = &mTracker->getCurrentPose();
	for (auto &p : mMap->getFeatures())
	{
		const Feature &feature = *p;
		
		Eigen::Vector4f color;
		auto match = mTracker->getMatch(&feature);
		if (match)
		{
			color = StaticColors::Blue();

			//Add line
			mImageLines.push_back(feature.getPosition());
			mImageLines.push_back(match->getPosition());
			mImageLineColors.push_back(StaticColors::Yellow());
			mImageLineColors.push_back(StaticColors::Blue());
		}
		else
			color = StaticColors::Green(0.7f);

		//mImagePoints.push_back(feature.getPosition());
		//mImagePointColors.push_back(color);
	}
	//auto &refPoints = mSystem->getTracker().refPoints;
	//auto &imgPoints = mSystem->getTracker().imgPoints;
	//for (int i = 0; i < refPoints.size(); i++)
	//{
	//	mImageLines.push_back(eutils::FromCV(refPoints[i]));
	//	mImageLines.push_back(eutils::FromCV(imgPoints[i]));
	//	mImageLineColors.push_back(StaticColors::Yellow());
	//	mImageLineColors.push_back(StaticColors::Blue());
	//}

	const float pointAlpha = 0.6f;
}

void MainWindow::resize()
{
    mTiler.configDevice(Eigen::Vector2i::Zero(),UserInterfaceInfo::Instance().getScreenSize(),3,3);

	mTiler.addTile(0, 0, -1.0f, 3, 2);
	mTiler.setImageMVP(0, 2*mImageSize);
	Eigen::Matrix4f offsetMat;
	offsetMat << 1, 0, 0, (float)mImageSize[0] / 2, 0, 1, 0, (float)mImageSize[1] / 2, 0, 0, 1, 0, 0, 0, 0, 1;
	mTiler.multiplyMVP(0, offsetMat);

	mTiler.addTile(0, 2);
	mTiler.setImageMVP(1, mImageSize);
	mTiler.addTile(1, 2);
	mTiler.setImageMVP(2, mImageSize);

	mTiler.addTile(2, 2);
	mTiler.setImageMVP(3, Eigen::Vector2i(100,100));
}

void MainWindow::draw()
{
	mTiler.setActiveTile(1);
	mShaders->getTexture().setMVPMatrix(mTiler.getMVP());
	mShaders->getTexture().renderTexture(mRefTexture.getTarget(), mRefTexture.getId(), mImageSize, 1.0f);

	mTiler.setActiveTile(2);
	mShaders->getTexture().setMVPMatrix(mTiler.getMVP());
	mShaders->getTexture().renderTexture(mCurrentImageTextureTarget, mCurrentImageTextureId, mImageSize, 1.0f);
	if (mTracker->getFrame())
	{
		std::vector<Eigen::Vector2f> points;
		for (auto &kp : mTracker->getFrame()->getKeypoints(0))
		{
			points.push_back(eutils::FromCV(kp.pt));
		}
		glPointSize(3.0f);
		mShaders->getColor().setMVPMatrix(mTiler.getMVP());
		mShaders->getColor().drawVertices(GL_POINTS, points.data(), points.size(), StaticColors::Blue());
	}

	mTiler.setActiveTile(0);
    mShaders->getTexture().setMVPMatrix(mTiler.getMVP());
	mShaders->getTextureWarp().setMVPMatrix(mTiler.getMVP());
	mShaders->getColor().setMVPMatrix(mTiler.getMVP());

	//Draw textures
	mShaders->getTexture().renderTexture(mRefTexture.getTarget(), mRefTexture.getId(), mImageSize, 1.0f);
	if (!mIsLost)
		mShaders->getTextureWarp().renderTexture(mCurrentImageTextureTarget, mCurrentImageTextureId, mTrackerPose, 0.5f, mImageSize);

	std::vector<Eigen::Vector2f> pointsSrc;
	pointsSrc.push_back(Eigen::Vector2f(0, 0));
	pointsSrc.push_back(Eigen::Vector2f(mImageSize[0], 0));
	pointsSrc.push_back(Eigen::Vector2f(mImageSize[0], mImageSize[1]));
	pointsSrc.push_back(Eigen::Vector2f(0, mImageSize[1]));

	mShaders->getWarpPos().setMVPMatrix(mTiler.getMVP());

	for (auto &framep : mMap->getKeyframes())
	{
		auto &frame = *framep;
		mShaders->getWarpPos().setHomography(frame.getPose());
		mShaders->getWarpPos().drawVertices(GL_LINE_LOOP, &pointsSrc[0], pointsSrc.size(), StaticColors::Gray());
	}

	mShaders->getWarpPos().setHomography(mSystem->getTracker().getCurrentPose());
	Eigen::Vector4f color;
	if (mSystem->getTracker().isLost())
		color = StaticColors::Red();
	else
		color = StaticColors::White();
	mShaders->getWarpPos().drawVertices(GL_LINE_LOOP, &pointsSrc[0], pointsSrc.size(), color);

	//Draw image lines
	mShaders->getColor().drawVertices(GL_LINES, mImageLines.data(), mImageLineColors.data(), mImageLines.size());

	//Draw image points
	glPointSize(8);
	mShaders->getColor().drawVertices(GL_POINTS, mImagePoints.data(), mImagePointColors.data(), mImagePoints.size());

	//K
	mTiler.setActiveTile(3);
	mShaders->getText().setMVPMatrix(mTiler.getMVP());
	mShaders->getText().setActiveFontSmall();
	mShaders->getText().setRenderCharHeight(4);
	mShaders->getText().setCaret(Eigen::Vector2f(0, 0));
	mShaders->getText().setColor(StaticColors::Green());
	{
		TextRendererStream ts(mShaders->getText());
		ts << "Alpha0=" << mSystem->getCalib().getInitialAlpha() << "\n";
		ts << "Normal=" << mSystem->getNormal() << "\nK=" << mSystem->getK() << "\nDistortion=" << mSystem->getDistortion().getCoefficients().transpose();
	}
}

void MainWindow::loadBouguetCalib()
{
	//Vars to read
	int imageCount;
	Eigen::Vector2i imageSize;
	std::vector<Eigen::Matrix2Xf> imagePoints;
	std::vector<Eigen::Matrix3f> homographies;

	std::string filename("Calib_results.mat");

	mat_t *matFile;
	matFile = Mat_Open(filename.c_str(), MAT_ACC_RDONLY);
	if (!matFile)
	{
		MYAPP_LOG << "Error opening mat file: " << filename << "\n";
		return;
	}

	matvar_t *matVar;

	//Read number of images
	Mat_Rewind(matFile); //Hack: must call or Mat_VarRead might fail
	matVar = Mat_VarRead(matFile,"n_ima");
	if (!matVar)
		throw std::runtime_error("Variable not found in mat file.");
	imageCount = (int)(*static_cast<double*>(matVar->data));
	MYAPP_LOG << "Reading calib info for " << imageCount << " images...";
	Mat_VarFree(matVar);


	//Read image width
	Mat_Rewind(matFile); //Hack: must call or Mat_VarRead might fail
	matVar = Mat_VarRead(matFile, "nx");
	if (!matVar)
		throw std::runtime_error("Variable not found in mat file.");
	imageSize[0] = (int)(*static_cast<double*>(matVar->data));
	Mat_VarFree(matVar);

	//Read image height
	Mat_Rewind(matFile); //Hack: must call or Mat_VarRead might fail
	matVar = Mat_VarRead(matFile, "ny");
	if (!matVar)
		throw std::runtime_error("Variable not found in mat file.");
	imageSize[1] = (int)(*static_cast<double*>(matVar->data));
	Mat_VarFree(matVar);

	//Read image data
	imagePoints.resize(imageCount);
	homographies.resize(imageCount);
	for (int i = 0; i < imageCount; i++)
	{
		//Image points
		{
			std::stringstream ss;
			ss << "x_" << (i + 1);
			Mat_Rewind(matFile); //Hack: must call or Mat_VarRead might fail
			matVar = Mat_VarRead(matFile, ss.str().c_str());
			if (!matVar)
				throw std::runtime_error("Variable not found in mat file.");
			assert(matVar->rank == 2);
			assert(matVar->dims[0] == 2);
			assert(matVar->data_type == MAT_T_DOUBLE);
			assert(matVar->class_type == MAT_C_DOUBLE);
			
			Eigen::Matrix2Xd points;
			points.resize(2, matVar->dims[1]);
			memcpy(points.data(), matVar->data, matVar->nbytes);
			imagePoints[i] = points.cast<float>();
			Mat_VarFree(matVar);
		}

		//Homography
		{
			std::stringstream ss;
			ss << "H_" << (i + 1);
			Mat_Rewind(matFile); //Hack: must call or Mat_VarRead might fail
			matVar = Mat_VarRead(matFile, ss.str().c_str());
			if (!matVar)
				throw std::runtime_error("Variable not found in mat file.");
			assert(matVar->rank == 2);
			assert(matVar->dims[0] == 3 && matVar->dims[1] == 3);
			assert(matVar->data_type == MAT_T_DOUBLE);
			assert(matVar->class_type == MAT_C_DOUBLE);
			Eigen::Matrix3d h;
			memcpy(h.data(), matVar->data, matVar->nbytes);
			homographies[i] = h.cast<float>();
			Mat_VarFree(matVar);
		}
	}
	
	Mat_Close(matFile);

	//Create map
	int featureCount = imagePoints[0].cols();
	std::unique_ptr<Map> map(new Map);
	
	//Create features
	for (int i = 0; i < featureCount; i++)
	{
		std::unique_ptr<Feature> feature(new Feature);
		feature->setPosition(imagePoints[0].col(i));
		map->addFeature(std::move(feature));
	}

	//Create keyframes
	cv::Mat3b nullImg3(imageSize[1],imageSize[0]);
	cv::Mat1b nullImg1(imageSize[1], imageSize[0]);
	Eigen::Matrix<uchar, 1, 32> nullDescr;
	nullDescr.setZero();
	Eigen::Matrix3f refHinv = homographies[0].inverse();
	for (int k = 0; k< imageCount; k++)
	{
		std::unique_ptr<Keyframe> frame(new Keyframe);
		
		frame->init(nullImg3, nullImg1);
		frame->setPose(homographies[k]*refHinv);

		for (int i = 0; i < featureCount; i++)
		{
			std::unique_ptr<FeatureMeasurement> m(new FeatureMeasurement(map->getFeatures()[i].get(), frame.get(), imagePoints[k].col(i), 0, nullDescr.data()));
			frame->getMeasurements().push_back(m.get());
			map->getFeatures()[i]->getMeasurements().push_back(std::move(m));
		}

		map->addKeyframe(std::move(frame));
	}

	mSystem->setMap(std::move(map));
	updateState();
}

void MainWindow::doHomographyBA()
{
	//RadialCameraDistortionModel model;
	//model.init(Eigen::Vector2f(0.16262f / (1759.9583f*1759.9583f), -0.67445f / (1759.9583f*1759.9583f*1759.9583f*1759.9583f)), mImageSize);
	//RadialCameraDistortionModel invModel = model.createInverseModel();


	mSystem->doHomographyBA();
}

void MainWindow::doFullBA()
{
	mSystem->doFullBA();
}

} /* namespace dtslam */
