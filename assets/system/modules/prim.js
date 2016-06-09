/**
 *	prim CommonJS module for Sphere 2.0
 *	(c) 2015-2016 Fat Cerberus
**/

module.exports =
{
    blit:        blit,
    circle:      circle,
    ellipse:     ellipse,
    lineCircle:  lineCircle,
    lineEllipse: lineEllipse,
    lineRect:    lineRect,
    rect:        rect,
    triangle:    triangle
};

function blit(surface, x, y, image, mask)
{
	mask = mask || Color.White;

	var x1 = x << 0;
	var y1 = y << 0;
	var x2 = x1 + image.width;
	var y2 = y1 + image.height;
	var shape = new Shape([
		{ x: x1, y: y1, u: 0.0, v: 1.0, color: mask },
		{ x: x2, y: y1, u: 1.0, v: 1.0, color: mask },
		{ x: x1, y: y2, u: 0.0, v: 0.0, color: mask },
		{ x: x2, y: y2, u: 1.0, v: 0.0, color: mask },
	], image, ShapeType.TriStrip);
	shape.draw(surface);
}

function circle(surface, x, y, radius, color, color2)
{
	ellipse(surface, x, y, radius, radius, color, color2);
}

function ellipse(surface, x, y, rx, ry, color, color2)
{
	color2 = color2 || color;

	var numSegments = 10 * Math.sqrt((rx + ry) / 2.0);
	var vertices = [ { x: x, y: y, color: color } ];
	var pi2 = 2 * Math.PI;
	var cos = Math.cos;
	var sin = Math.sin;
	for (var i = 0; i < numSegments; ++i) {
		var phi = pi2 * i / numSegments;
		var c = cos(phi);
		var s = sin(phi);
		vertices.push({
			x: x + c * rx,
			y: y - s * ry,
			color: color2,
		});
	}
	vertices.push({
		x: x + rx,  // cos(0) = 1.0
		y: y,       // sin(0) = 0.0
		color: color2,
	});

	var shape = new Shape(vertices, null, ShapeType.Fan);
	shape.draw(surface);
}

function lineCircle(surface, x, y, radius, color, color2)
{
	lineEllipse(surface, x, y, radius, radius, color, color2);
}

function lineEllipse(surface, x, y, rx, ry, color)
{
	var numSegments = 10 * Math.sqrt((rx + ry) / 2.0);
	var vertices = [];
	var pi2 = 2 * Math.PI;
	var cos = Math.cos;
	var sin = Math.sin;
	for (var i = 0; i < numSegments - 1; ++i) {
		var phi = pi2 * i / numSegments;
		var c = cos(phi);
		var s = sin(phi);
		vertices.push({
			x: x + c * rx,
			y: y - s * ry,
			color: color,
		});
	}
	var shape = new Shape(vertices, null, ShapeType.LineLoop)
	shape.draw(surface);
}

function lineRect(surface, x, y, width, height, color)
{
	// align to pixel centers
	x = (x << 0) + 0.5;
	y = (y << 0) + 0.5;

	var shape = new Shape([
		{ x: x, y: y, color: color },
		{ x: x + width, y: y, color: color },
		{ x: x + width, y: y + height, color: color },
		{ x: x, y: y + height, color: color },
	], null, ShapeType.LineLoop);
	shape.draw(surface);
}

function rect(surface, x, y, width, height, color_ul, color_ur, color_lr, color_ll)
{
	color_ur = color_ur || color_ul;
	color_lr = color_lr || color_ul;
	color_ll = color_ll || color_ul;

	var shape = new Shape([
		{ x: x, y: y, color: color_ul },
		{ x: x + width, y: y, color: color_ur },
		{ x: x, y: y + height, color: color_ll },
		{ x: x + width, y: y + height, color: color_lr },
	], null, ShapeType.TriStrip);
	shape.draw(surface);
}

function triangle(surface, x1, y1, x2, y2, x3, y3, color1, color2, color3)
{
	color2 = color2 || color1;
	color3 = color3 || color1;
	
	var shape = new Shape([
		{ x: x1, y: y1, color: color1 },
		{ x: x2, y: y2, color: color2 },
		{ x: x3, y: y3, color: color3 },
	], null, ShapeType.Triangles);
}
