#include "renderitem.h"

class SwapContext
{
public:
	SwapContext(QOpenGLContext *ctx, QSurface *surf)
	{
		p_ctx = QOpenGLContext::currentContext();
		p_surf = p_ctx->surface();
		ctx->makeCurrent(surf);
	}
	~SwapContext()
	{
		p_ctx->makeCurrent(p_surf);
	}

private:
	QOpenGLContext *p_ctx;
	QSurface *p_surf;
};

RenderItem::RenderItem() : input_polygon(PrintPolygon::Instance()), m_input(false), m_inputRay(false)
{
	connect(this, SIGNAL(windowChanged(QQuickWindow*)), this, SLOT(handleWindowChanged(QQuickWindow*)));
	ctx.reset(new QOpenGLContext(this));
	ctx->create();
}

bool RenderItem::isInput() const
{
	return m_input;
}

void RenderItem::setInput(bool val)
{
	if (m_input == val)
		return;

	m_input = val;

	emit inputChanged(val);
}

std::pair<float, float> RenderItem::pointToGL(int point_x, int point_y)
{
	int m_width = this->window()->width();
	int m_height = this->window()->height();
	float ratio = (float)m_height / (float)m_width;
	float XPixelSize = 2.0f * (1.0f * point_x / m_width);
	float YPixelSize = 2.0f * (1.0f * point_y / m_height);
	float Xposition = XPixelSize - 1.0f;
	float Yposition = (2.0f - YPixelSize) - 1.0f;
	if (m_width >= m_height)
		Xposition /= ratio;
	else
		Yposition *= ratio;

	return{ Xposition, Yposition };
}

void RenderItem::doSendPoint(int point_x, int point_y)
{
	SwapContext set_ctx(ctx.get(), window());
	glm::vec2 point;
	std::tie(point.x, point.y) = pointToGL(point_x, point_y);
	input_polygon.add_point(point);
	m_points.push_back(point);
	if (!m_inputRay && m_points.size() >= 3)
	{
		finishBox();
	}
	else if (m_inputRay && m_points.size() >= 2)
	{
		input_polygon.clear_point();
		setInput(false);
		Birefringence();
	}
}

void RenderItem::doSendFuturePoint(int point_x, int point_y)
{
	SwapContext set_ctx(ctx.get(), window());
	glm::vec2 point;
	std::tie(point.x, point.y) = pointToGL(point_x, point_y);
	input_polygon.future_point(point);
}

void RenderItem::doButton()
{
	cleanScene();
	m_inputRay = false;
	setInput(true);
	emit inputChanged(m_input);
	emit setLabel("Select 3 points parallelepiped base");
}

void RenderItem::handleWindowChanged(QQuickWindow *win)
{
	if (win)
	{
		connect(win, SIGNAL(beforeSynchronizing()), this, SLOT(sync()), Qt::DirectConnection);
		connect(win, SIGNAL(sceneGraphInvalidated()), this, SLOT(cleanup()), Qt::DirectConnection);
		win->setClearBeforeRendering(false);
	}
}

void RenderItem::cleanScene()
{
	SwapContext set_ctx(ctx.get(), window());
	input_polygon.clear();
	m_points.clear();
}

void RenderItem::finishBox()
{
	if (m_points.size() != 3)
		return;
	glm::vec2 a = m_points[0] - m_points[1];
	glm::vec2 b = m_points[2] - m_points[1];
	m_points.push_back(m_points[1] + (a + b));
	input_polygon.add_point(m_points.back());

	m_points.push_back(m_points.front());
	input_polygon.add_point(m_points.back());

	input_polygon.finish_box();

	m_box = std::move(m_points);
	m_points.clear();

	emit setLabel("Select the direction of the ray");
	m_inputRay = true;
}

std::tuple<float, float, float> getEquation(glm::vec2 a, glm::vec2 b)
{
	glm::vec2 v = b - a;
	float A = -v.y;
	float B = v.x;
	float C = -(A * a.x + B * a.y);
	return std::make_tuple(A, B, C);
}

bool Ray_accessory(std::vector<std::pair<float, float> > v)
{
	auto mdist = [](std::pair<float, float> &a, std::pair<float, float> &b) -> float
	{
		return sqrt((a.first - b.first) * (a.first - b.first) + (a.second - b.second) * (a.second - b.second));
	};

	v.push_back(std::make_pair(v[2].first - v[1].first, v[2].second - v[1].second));
	v.push_back(std::make_pair(v[0].first - v[1].first, v[0].second - v[1].second));
	float x = fabs(v[3].first * v[4].second - v[3].second * v[4].first);
	float a = mdist(v[1], v[2]);
	float ans = x / a;
	float x1 = v[2].first - v[1].first, y1 = v[2].second - v[1].second;
	float x2 = v[0].first - v[1].first, y2 = v[0].second - v[1].second;
	float f = acos((x1*x2 + y1*y2) / sqrt(x1*x1 + y1*y1) / sqrt(x2*x2 + y2*y2));
	if (f > acos(-1.) / 2.0)
		ans = mdist(v[0], v[1]);
	return (fabs(ans) < 1e-6);
}

std::pair<int, glm::vec2> RenderItem::cross_with_box(glm::vec2 c, glm::vec2 d)
{
	std::vector<glm::vec2> mcross;
	std::vector<int> mcross_id;

	for (int i = 0; i < (int)m_box.size() - 1; ++i)
	{
		glm::vec2 a = m_box[i];
		glm::vec2 b = m_box[i + 1];

		float A1, B1, C1, A2, B2, C2;

		std::tie(A1, B1, C1) = getEquation(a, b);
		std::tie(A2, B2, C2) = getEquation(c, d);

		float xcross = -(C1 * B2 - C2 * B1) / (A1 * B2 - A2 * B1);
		float ycross = -(A1 * C2 - A2 * C1) / (A1 * B2 - A2 * B1);

		float xmin = std::min(a.x, b.x);
		float xmax = std::max(a.x, b.x);

		float ymin = std::min(a.y, b.y);
		float ymax = std::max(a.y, b.y);
		float eps = 1e-6f;
		if ((xcross > xmin || (fabs(xcross - xmin) < eps)) && (xcross < xmax || (fabs(xcross - xmax) < eps)) &&
			(ycross > ymin || (fabs(ycross - ymin) < eps)) && (ycross < ymax || (fabs(ycross - ymax) < eps)))
		{
			mcross.push_back(glm::vec2(xcross, ycross));
			mcross_id.push_back(i);
		}
	}
	glm::vec2 point_cross;
	int idWrite = -1;
	float dist_to_cross = 1e9;
	for (int i = 0; i < (int)mcross.size(); ++i)
	{
		if (Ray_accessory
			(
		{
			{ mcross[i].x, mcross[i].y },
			{ c.x, c.y },
			{ d.x, d.y }
		}))
		{
			float _d = dist(mcross[i], d);
			if (_d < dist_to_cross)
			{
				dist_to_cross = _d;
				point_cross = mcross[i];
				idWrite = mcross_id[i];
			}
		}
	}
	return{ idWrite, point_cross };
}

float getAngle(glm::vec2 a, glm::vec2 b)
{
	return acos((a.x*b.x + a.y*b.y) / sqrt(a.x*a.x + a.y*a.y) / sqrt(b.x*b.x + b.y*b.y));
}

glm::vec2 rotate(glm::vec2 point, float angle)
{
	glm::vec2 rotated_point;
	rotated_point.x = point.x * cos(angle) - point.y * sin(angle);
	rotated_point.y = point.x * sin(angle) + point.y * cos(angle);
	float d = dist(point, rotated_point);
	return rotated_point;
}

glm::vec2 calc_refraction(glm::vec2 ray_a, glm::vec2 ray_b, glm::vec2 normal, float n1, float n2)
{
	normal = 2.0f * glm::normalize(normal);

	if (dist(ray_a, ray_b + normal) < dist(ray_a, ray_b - normal))
		normal = -normal;

	glm::vec2 vec_ray = ray_b - ray_a;
	float alpha = getAngle(normal, vec_ray);
	float sinAlpha = sin(alpha);

	float sinBetta = sinAlpha * n1 / n2;

	float betta = asin(sinBetta);
	glm::vec2 veca = ray_b - ray_a;
	glm::vec2 vecb = ray_b - normal;
	if ((veca.x * vecb.y - veca.y * vecb.x) < 0.0f)
		betta = -betta;

	return ray_b + rotate(normal, betta);
}

void RenderItem::Birefringence()
{
	assert(m_box.size() == 5u && m_points.size() == 2u);

	glm::vec2 c = m_points[0];
	glm::vec2 d = m_points[1];
	glm::vec2 vec_ray = d - c;

	glm::vec2 point_cross;
	int idWrite;
	std::tie(idWrite, point_cross) = cross_with_box(c, d);

	if (idWrite == -1)
	{
		std::vector<line> output;
		output.push_back(std::make_pair(c, c + (d - c) * 10.0f));
		input_polygon.set_ray(output);
		return;
	}

	glm::vec2 normal;

	std::tie(normal.x, normal.y, std::ignore) = getEquation(m_box[idWrite], m_box[idWrite + 1]);
	glm::vec2 point_ref_res = calc_refraction(c, point_cross, normal, 1.0f, 1.33f);

	glm::vec2 point_cross_out;
	std::tie(idWrite, point_cross_out) = cross_with_box(point_cross, point_ref_res);
	assert(idWrite != -1);

	glm::vec2 normal_out;
	std::tie(normal_out.x, normal_out.y, std::ignore) = getEquation(m_box[idWrite], m_box[idWrite + 1]);
	glm::vec2 point_ref_res_end = calc_refraction(point_cross, point_cross_out, normal_out, 1.33f, 1.0f);

	std::vector<line> output;
	output.push_back(std::make_pair(c, point_cross));
	output.push_back(std::make_pair(point_cross, point_cross_out));
	if (!std::isnan(point_ref_res_end.x))
		output.push_back(std::make_pair(point_cross_out, point_ref_res_end));
	else
		emit setLabel("Total internal reflection");
	input_polygon.set_ray(output);
}

void RenderItem::paint()
{
	paintPolygon();
}

void RenderItem::paintPolygon()
{
	SwapContext set_ctx(ctx.get(), window());
	input_polygon.draw();
}

void RenderItem::sync()
{
	SwapContext set_ctx(ctx.get(), window());
	static bool initSt = false;
	if (!initSt)
	{
		ogl_LoadFunctions();
		connect(window(), SIGNAL(beforeRendering()), this, SLOT(paint()), Qt::DirectConnection);
		initSt = input_polygon.init();
	}
	input_polygon.resize(0, 0, window()->width(), window()->height());
}

void RenderItem::cleanup()
{
	SwapContext set_ctx(ctx.get(), window());
	input_polygon.destroy();
}
