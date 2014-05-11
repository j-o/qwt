/* -*- mode: C++ ; c-file-style: "stroustrup" -*- *****************************
 * Qwt Widget Library
 * Copyright (C) 1997   Josef Wilgen
 * Copyright (C) 2002   Uwe Rathmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the Qwt License, Version 1.0
 *****************************************************************************/

#include "qwt_spline.h"
#include "qwt_math.h"
#include <QDebug>

static inline void qwtToBezier(
    double x1, double y1, double cv1, 
    double x2, double y2, double cv2, 
    double &cpx1, double &cpy1, double &cpx2, double &cpy2 )
{
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double stepX = dx / 3.0;

    const double c = dy / dx - ( 0.5 * cv2 + cv1 ) * stepX;

    cpx1 = x1 + stepX;
    cpy1 = y1 + c * stepX;

    cpx2 = x2 - stepX;
    cpy2 = cpy1 + ( c + 0.5 * cv1 * dx ) * stepX;
}

static inline void qwtCubicTo( const QPointF &p1, double m1,
    const QPointF &p2, double m2, QPainterPath &path )
{
    const double dx = ( p2.x() - p1.x() ) / 3.0;
    path.cubicTo( p1 + QPointF( dx, m1 * dx ), 
        p2 - QPointF( dx, m2 * dx ), p2 );
}

static inline void qwtNaturalCubicTo( const QPointF &p1, double cv1,
    const QPointF &p2, double cv2, QPainterPath &path )
{
    const double dx = p2.x() - p1.x();
    const double dy = p2.y() - p1.y();
    const double stepX = dx / 3.0;

    const double c = dy / dx - ( 0.5 * cv2 + cv1 ) * stepX;

    const double cy1 = p1.y() + c * stepX;
    const double cy2 = cy1 + ( c + 0.5 * cv1 * dx ) * stepX; 

    path.cubicTo( p1.x() + stepX, cy1, 
        p2.x() - stepX, cy2, p2.x(), p2.y() );
}

QVector<double> QwtSplineNatural::curvatures( const QPolygonF &points )
{
    const int size = points.size();

    QVector<double> aa0( size - 1 );
    QVector<double> bb0( size - 1 );

    {
        double dx1 = points[1].x() - points[0].x();
        double slope1 = ( points[1].y() - points[0].y() ) / dx1;

        for ( int i = 1; i < size - 1; i++ )
        {
            const double dx2 = points[i+1].x() - points[i].x();
            const double slope2 = ( points[i+1].y() - points[i].y() ) / dx2;

            aa0[i] = 2.0 * ( dx1 + dx2 );
            bb0[i] = 6.0 * ( slope1 - slope2 );

            slope1 = slope2;
            dx1 = dx2;
        }
    }

    // L-U Factorization
    QVector<double> cc0( size - 1 );
    for ( int i = 1; i < size - 2; i++ )
    {
        const double dx = points[i+1].x() - points[i].x();

        cc0[i] = dx / aa0[i];
        aa0[i+1] -= dx * cc0[i];
    }

    // forward elimination
    QVector<double> s( size );
    s[1] = bb0[1];
    for ( int i = 2; i < size - 1; i++ )
    {
        s[i] = bb0[i] - cc0[i-1] * s[i-1];
    }

    bb0.clear();
    cc0.clear();

    // backward elimination
    s[size - 2] = - s[size - 2] / aa0[size - 2];
    for ( int i = size - 3; i > 0; i-- )
    {
        const double dx = points[i+1].x() - points[i].x();
        s[i] = - ( s[i] + dx * s[i+1] ) / aa0[i];
    }

    s[size - 1] = s[0] = 0.0;

    return s;
}

static inline void qwtNaturalCoeff(double b1, double b2,
    const QPointF &p1, const QPointF &p2, double &a, double &c )
{
    const double dx = p2.x() - p1.x();
    const double dy = p2.y() - p1.y();

    a = ( b2 - b1 ) / ( 3.0 * dx );
    c = dy / dx - ( b2 + 2.0 * b1 ) * dx / 3.0;
}

QPolygonF QwtSplineNatural::polygon( const QPolygonF &points, int numPoints )
{
    const int size = points.size();
    if ( size <= 2 )
        return points;

    const QVector<double> cv = curvatures( points );

    const QPointF *p = points.constData();

    const double x1 = points.first().x();
    const double x2 = points.last().x();
    const double delta = ( x2 - x1 ) / ( numPoints - 1 );

    double a, b, c, x0, y0;

    QPolygonF fittedPoints;

    for ( int i = 0, j = 0; i < numPoints; i++ )
    {
        double x = x1 + i * delta;
        if ( x > x2 )
            x = x2;

        if ( i == 0 || x > p[j + 1].x() )
        {
            while ( x > p[j + 1].x() )
                j++;

            toCoefficients( 
                p[j].x(), p[j].y(), cv[j],
                p[j + 1].x(), p[j + 1].y(), cv[j + 1], 
                a, b, c );

            x0 = p[j].x();
            y0 = p[j].y();
        }

        const double y = qwtCubicPolynom( x - x0, a, b, c, y0 );
        fittedPoints += QPointF( x, y );
    }

    return fittedPoints;
}

QPainterPath QwtSplineNatural::path( const QPolygonF &points )
{
    QPainterPath path;

    const int size = points.size();
    if ( size <= 2 )
    {
        path.addPolygon( points );
        return path;
    }

    const QVector<double> cv = curvatures( points );
    const QPointF *p = points.constData();

    path.moveTo( p[0] );
    for ( int i = 0; i < size - 1; i++ )
        qwtNaturalCubicTo( p[i], cv[i], p[i+1], cv[i+1], path );

    return path;
}

// ---

static inline double qwtSlope( const QPointF &p1, const QPointF &p2 )
{
    const double dx = p2.x() - p1.x();
    return dx ? ( p2.y() - p1.y() ) / dx : 0.0;
}

static inline double qwtAkima( double s1, double s2, double s3, double s4 )
{
    if ( ( s1 == s2 ) && ( s3 == s4 ) )
    {
        return 0.5 * ( s2 + s3 );
    }

    const double ds12 = qAbs( s2 - s1 );
    const double ds34 = qAbs( s4 - s3 );

    return ( s2 * ds34 + s3 * ds12 ) / ( ds12 + ds34 );
}

QPainterPath QwtSplineAkima::path( const QPolygonF &points )
{
    const int size = points.size();
    if ( size == 0 )
        return QPainterPath();

    if ( size == 1 )
    {
        QPainterPath path;
        path.moveTo( points[0] );
        return path;
    }

    const double m1 = qwtSlope( points[0], points[1] );
    const double m2 = qwtSlope( points[size-2], points[size-1] );

    return path( points, m1, m2 );
}

QPainterPath QwtSplineAkima::path( const QPolygonF &points, 
    double slopeStart, double slopeEnd )
{
    QPainterPath path;

    const int size = points.size();
    if ( size == 0 )
        return path;

    const QPointF *p = points.constData();
    path.moveTo( p[0] );

    if ( size == 1 )
        return path;

    if ( size == 2 )
    {
        qwtCubicTo( points[0], slopeStart, points[1], slopeEnd, path );
        return path;
    }

    double slope1 = slopeStart;
    double slope2 = qwtSlope( p[0], p[1] );
    double slope3 = qwtSlope( p[1], p[2] );

    double m1 = slope1;

    for ( int i = 0; i < size - 3; i++ )
    {
        const double slope4 = qwtSlope( p[i+2],  p[i+3] );

        const double m2 = qwtAkima( slope1, slope2, slope3, slope4 );
        qwtCubicTo( p[i], m1, p[i+1], m2, path );

        slope1 = slope2;
        slope2 = slope3;
        slope3 = slope4;

        m1 = m2;
    }

    const double m2 = qwtAkima( slope1, slope2, slope3, slopeEnd );

    qwtCubicTo( p[size - 3], m1, p[size - 2], m2, path );
    qwtCubicTo( p[size - 2], m2, p[size - 1], slopeEnd, path );

    return path;
}

static inline double qwtHarmonicMean( 
    double dx1, double dy1, double dx2, double dy2 )
{
    if ( ( dy1 > 0.0 ) == ( dy2 > 0.0 ) )
    {
        if ( ( dy1 != 0.0 ) && ( dy2 != 0.0 ) )
            return 2.0 / ( dx1 / dy1 + dx2 / dy2 );
    }

    return 0.0;
}

static inline double qwtHarmonicMean( double s1, double s2 )
{
    if ( ( s1 > 0.0 ) == ( s2 > 0.0 ) )
    {
        if ( ( s1 != 0.0 ) && ( s2 != 0.0 ) )
            return 2 / ( 1 / s1 + 1 / s2 );
    }

    return 0.0;
}

QPainterPath QwtSplineHarmonicMean::path( const QPolygonF &points )
{
    const int size = points.size();
    if ( size == 0 )
        return QPainterPath();

    if ( size == 1 )
    {
        QPainterPath path;
        path.moveTo( points[0] );
        return path;
    }

    const double s1 = qwtSlope( points[0], points[1] );
    const double s2 = qwtSlope( points[1], points[2] );
    const double s3 = qwtSlope( points[size-3], points[size-2] );
    const double s4 = qwtSlope( points[size-2], points[size-1] );

    const double m1 = 1.5 * s1 - 0.5 * qwtHarmonicMean( s1, s2 );
    const double m2 = 1.5 * s4 - 0.5 * qwtHarmonicMean( s3, s4 );

    return path( points, m1, m2 );
}

QPainterPath QwtSplineHarmonicMean::path( const QPolygonF &points, 
    double slopeStart, double slopeEnd )
{
    QPainterPath path;

    const int size = points.size();
    if ( size == 0 )
        return path;

    const QPointF *p = points.constData();
    path.moveTo( p[0] );

    if ( size == 1 )
        return path;

    if ( size == 2 )
    {
        qwtCubicTo( points[0], slopeStart, points[1], slopeEnd, path );
        return path;
    }

    double dx1 = p[1].x() - p[0].x();
    double dy1 = p[1].y() - p[0].y();
    double m1 = slopeStart;

    for ( int i = 1; i < size - 1; i++ )
    {
        const double dx2 = p[i+1].x() - p[i].x();
        const double dy2 = p[i+1].y() - p[i].y();

        const double m2 = qwtHarmonicMean( dx1, dy1, dx2, dy2 );
        path.cubicTo( p[i-1] + QPointF( dx1, dx1 * m1 ) / 3.0,
            p[i] - QPointF( dx1, dx1 * m2 ) / 3.0, p[i] );

        dx1 = dx2;
        dy1 = dy2;
        m1 = m2;
    }

    path.cubicTo( p[size - 2] + QPointF( dx1, dx1 * m1 ) / 3.0,
        p[size - 1] - QPointF( dx1, dx1 * slopeEnd ) / 3.0, p[size - 1] );

    return path; 
}

QPainterPath QwtSplineBessel::path( const QPolygonF &points, 
    double slopeStart, double slopeEnd )
{
    QPainterPath path;

    const int size = points.size();
    if ( size == 0 )
        return path;

    const QPointF *p = points.constData();
    path.moveTo( p[0] );

    if ( size == 1 )
        return path;

    if ( size == 2 )
    {
        qwtCubicTo( points[0], slopeStart, points[1], slopeEnd, path );
        return path;
    }

    double slope1 = slopeStart;
    double dx1 = p[1].x() - p[0].x();
    double m1 = slopeStart;

    for ( int i = 0; i < size - 2; i++ )
    {
        const double dx2 = p[i+1].x() - p[i].x();
        double slope2 = ( p[i+1].y() - p[i].y() ) / dx2;

        const double m2 = ( dx2 * slope1 + dx1 * slope2 ) / ( dx1 + dx2 );
        qwtCubicTo( points[i], m1, points[i+1], m2, path );

        dx1 = dx2;
        slope1 = slope2;
        m1 = m2;
    }

    const double m2 = 0.5 * ( slope1 + slopeEnd ); 
    qwtCubicTo( points[size-2], m1, points[size-1], m2, path );

    return path; 
}
