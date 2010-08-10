#include "ptracer_proc.h"

MTS_NAMESPACE_BEGIN
	
/* ==================================================================== */
/*                           Work result impl.                          */
/* ==================================================================== */

void CaptureParticleWorkResult::load(Stream *stream) {
	Assert(sizeof(Spectrum) == sizeof(Float)*SPECTRUM_SAMPLES);
	size_t nEntries = fullSize.x * fullSize.y;
	stream->readFloatArray(reinterpret_cast<Float *>(pixels), nEntries*SPECTRUM_SAMPLES);
	m_range->load(stream);
}

void CaptureParticleWorkResult::save(Stream *stream) const {
	Assert(sizeof(Spectrum) == sizeof(Float)*SPECTRUM_SAMPLES);
	size_t nEntries = fullSize.x * fullSize.y;
	stream->writeFloatArray(reinterpret_cast<Float *>(pixels), nEntries*SPECTRUM_SAMPLES);
	m_range->save(stream);
}

/* ==================================================================== */
/*                         Work processor impl.                         */
/* ==================================================================== */

void CaptureParticleWorker::prepare() {
	ParticleTracer::prepare();
	m_camera = static_cast<Camera *>(getResource("camera"));
	m_isPinholeCamera = m_camera->getClass()->derivesFrom(PinholeCamera::m_theClass);
	m_filter = m_camera->getFilm()->getTabulatedFilter();
}

ref<WorkProcessor> CaptureParticleWorker::clone() const {
	return new CaptureParticleWorker(m_maxDepth, m_multipleScattering, 
		m_rrDepth);
}

ref<WorkResult> CaptureParticleWorker::createWorkResult() const {
	const Film *film = m_camera->getFilm();
	const int border = (int) std::ceil(std::max(m_filter->getFilterSize().x,
		m_filter->getFilterSize().y) - 0.5f);
	return new CaptureParticleWorkResult(film->getCropOffset(), film->getCropSize(), border);
}

void CaptureParticleWorker::process(const WorkUnit *workUnit, WorkResult *workResult, 
	const bool &stop) {
	const RangeWorkUnit *range = static_cast<const RangeWorkUnit *>(workUnit);
	m_workResult = static_cast<CaptureParticleWorkResult *>(workResult);
	m_workResult->setRangeWorkUnit(range);
	m_workResult->clear();
	ParticleTracer::process(workUnit, workResult, stop);
	m_workResult = NULL;
}

void CaptureParticleWorker::handleSurfaceInteraction(int, bool,
		const Intersection &its, const Spectrum &weight) {
	Point2 screenSample;

	if (m_camera->positionToSample(its.p, screenSample)) {
		Point cameraPosition = m_camera->getPosition(screenSample);
		if (m_scene->isOccluded(cameraPosition, its.p)) 
			return;

		const BSDF *bsdf = its.shape->getBSDF();
		Vector d = cameraPosition - its.p;
		Float dist = d.length(); d /= dist;

		BSDFQueryRecord bRec(its, its.toLocal(d));
		bRec.quantity = EImportance;

		Float importance; 
		if (m_isPinholeCamera)
			importance = ((PinholeCamera *) m_camera.get())->importance(screenSample) / (dist * dist);
		else
			importance = 1/m_camera->areaDensity(screenSample);

		/* Compute Le * importance and store it in an accumulation buffer */
		Ray ray(its.p, d, 0, dist);
		Spectrum sampleVal = weight * bsdf->fCos(bRec) 
			* m_scene->getAttenuation(ray) * importance;

		m_workResult->splat(screenSample, sampleVal, m_filter);
	}
}

void CaptureParticleWorker::handleMediumInteraction(int, bool, 
		const MediumSamplingRecord &mRec, const Vector &wi, 
		const Spectrum &weight) {
	Point2 screenSample;

	if (m_camera->positionToSample(mRec.p, screenSample)) {
		Point cameraPosition = m_camera->getPosition(screenSample);
		if (m_scene->isOccluded(cameraPosition, mRec.p))
			return;

		Vector wo = cameraPosition - mRec.p;
		Float dist = wo.length(); wo /= dist;

		Float importance; 
		if (m_isPinholeCamera)
			importance = ((PinholeCamera *) m_camera.get())->importance(screenSample) / (dist * dist);
		else
			importance = 1/m_camera->areaDensity(screenSample);

		/* Compute Le * importance and store in accumulation buffer */
		Ray ray(mRec.p, wo, 0, dist);

		Spectrum sampleVal = weight * mRec.medium->getPhaseFunction()->f(mRec, wi, wo)
			* m_scene->getAttenuation(ray) * importance;

		m_workResult->splat(screenSample, sampleVal, m_filter);
	}
}

/* ==================================================================== */
/*                        Parallel process impl.                        */
/* ==================================================================== */

void CaptureParticleProcess::develop() {
	float *accumImageData = m_accumBitmap->getFloatData();
	float *finalImageData = m_finalBitmap->getFloatData();
	size_t size = m_accumBitmap->getWidth() * m_accumBitmap->getHeight() * 4;
	Float weight = (m_accumBitmap->getWidth() * m_accumBitmap->getHeight()) 
		/ (Float) m_receivedResultCount;
	for (size_t i=0; i<size; i+=4) {
		for (int j=0; j<3; ++j) 
			finalImageData[i+j] = accumImageData[i+j] * weight;
		finalImageData[i+3] = 1.0f;
	}
	m_film->fromBitmap(m_finalBitmap);

	m_queue->signalRefresh(m_job);
}

void CaptureParticleProcess::processResult(const WorkResult *wr, bool cancelled) {
	const CaptureParticleWorkResult *result 
		= static_cast<const CaptureParticleWorkResult *>(wr);
	const RangeWorkUnit *range = result->getRangeWorkUnit();
	if (cancelled) 
		return;

	m_resultMutex->lock();
	increaseResultCount(range->getSize());

	/* Accumulate the received pixel data */
	float *imageData = m_accumBitmap->getFloatData();
	size_t pixelIndex, imagePixelIndex = 0;
	Float r, g, b;
	Vector2i start(result->getBorder(), result->getBorder());
	Vector2i end(result->getFullSize().x - result->getBorder(), 
		result->getFullSize().y - result->getBorder());

	for (int y=start.y; y<end.y; ++y) {
		pixelIndex = y*result->getFullSize().x + start.x;
		for (int x=start.x; x<end.x; ++x) {
			Spectrum spec(result->getPixel(pixelIndex));
			spec.toLinearRGB(r,g,b);
			imageData[imagePixelIndex++] += r;
			imageData[imagePixelIndex++] += g;
			imageData[imagePixelIndex++] += b;
			++imagePixelIndex;
			++pixelIndex;
		}
	}

	develop();

	m_resultMutex->unlock();
}

void CaptureParticleProcess::bindResource(const std::string &name, int id) {
	if (name == "camera") {
		Camera *camera = static_cast<Camera *>(Scheduler::getInstance()->getResource(id));
		m_film = camera->getFilm();
		const Vector2i res(m_film->getCropSize());
		m_accumBitmap = new Bitmap(res.x, res.y, 128);
		m_finalBitmap = new Bitmap(res.x, res.y, 128);
		m_accumBitmap->clear();
	}
	ParticleProcess::bindResource(name, id);
}

ref<WorkProcessor> CaptureParticleProcess::createWorkProcessor() const {
	return new CaptureParticleWorker(m_maxDepth, m_multipleScattering, 
		m_rrDepth);
}

MTS_IMPLEMENT_CLASS(CaptureParticleProcess, false, ParticleProcess)
MTS_IMPLEMENT_CLASS(CaptureParticleWorkResult, false, ImageBlock)
MTS_IMPLEMENT_CLASS_S(CaptureParticleWorker, false, ParticleTracer)
MTS_NAMESPACE_END
