/**
 *	prim CommonJS module for Sphere 2.0
 *	(c) 2015-2016 Fat Cerberus
**/

module.exports =
{
    circle: circle,
    rect:   rect,
};

const cos = Math.cos;
const sin = Math.sin;

function circle(surface, x, y, radius, color, color2)
{
	color2 = color2 || color;

	var numSegments = Math.min(radius, 128);
	var vertices = [ { x: x, y: y, u: 0.5, v: 0.5, color: color } ];
	var pi2 = 2 * Math.PI;
	for (var i = 0; i < numSegments; ++i) {
		var phi = pi2 * i / numSegments;
		var c = cos(phi), s = sin(phi);
		vertices.push({
			x: x + c * radius,
			y: y - s * radius,
			u: (c + 1.0) / 2.0,
			v: (s + 1.0) / 2.0,
			color: color2,
		});
	}
	vertices.push({
		x: x + radius,  // cos(0) = 1.0
		y: y,           // sin(0) = 0.0
		u: 1.0, v: 0.5,
		color: color2,
	});
	
	var shape = new Shape(vertices, null, ShapeType.Fan);
	shape.draw(surface);
}

function rect(surface, x, y, width, height, color)
{
	var shape = new Shape([
		{ x: x, y: y, color: color },
		{ x: x + width, y: y, color: color },
		{ x: x, y: y + height, color: color },
		{ x: x + width, y: y + height, color: color },
	], null, ShapeType.TriStrip);
	shape.draw(surface);
}
